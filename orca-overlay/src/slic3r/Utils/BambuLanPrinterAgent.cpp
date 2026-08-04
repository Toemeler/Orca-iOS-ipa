// Bambu LAN backend - IPrinterAgent implementation. See BambuLanPrinterAgent.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanPrinterAgent.hpp"

#include "BambuLanFtps.hpp"
#include "BambuLanPrintCommand.hpp"

#include <sys/stat.h>

#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

const char* const BAMBU_LAN_AGENT_VERSION = "1.0.0";

// 1 GB, the same ceiling the plugin enforced.
constexpr long long MAX_PRINT_FILE_SIZE = 1024LL * 1024LL * 1024LL;

std::string basename_of(const std::string& path)
{
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string extension_of(const std::string& path)
{
    const std::string name = basename_of(path);
    // ".gcode.3mf" is a compound extension worth keeping whole.
    const size_t gcode_3mf = name.rfind(".gcode.3mf");
    if (gcode_3mf != std::string::npos && gcode_3mf + 10 == name.size())
        return ".gcode.3mf";
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : name.substr(dot);
}

long long file_size_of(const std::string& path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return -1;
    return static_cast<long long>(st.st_size);
}

// The name the file gets on the printer's SD card. A project name makes the
// printer's own file list readable; the sliced file's name is the fallback.
std::string remote_name_for(const PrintParams& params)
{
    if (!params.project_name.empty())
        return BambuLan::sanitize_remote_file_name(params.project_name + extension_of(params.filename));
    return BambuLan::sanitize_remote_file_name(params.filename);
}

void notify(const OnUpdateStatusFn& fn, int stage, int code, const std::string& msg)
{
    if (fn)
        fn(stage, code, msg);
}

bool cancelled(const WasCancelledFn& fn) { return fn && fn(); }

} // namespace

BambuLanPrinterAgent::BambuLanPrinterAgent(std::string log_dir) : m_log_dir(std::move(log_dir))
{
    m_mqtt.set_message_fn([this](const std::string& topic, const std::string& payload) { on_mqtt_message(topic, payload); });
    m_mqtt.set_lost_fn([this](const std::string& reason) { on_mqtt_lost(reason); });
    m_ssdp.set_printer_fn([this](const std::string& json_str) {
        if (m_on_ssdp_msg_fn)
            m_on_ssdp_msg_fn(json_str);
    });
    BOOST_LOG_TRIVIAL(info) << "BambuLanPrinterAgent: native LAN agent created";
}

BambuLanPrinterAgent::~BambuLanPrinterAgent()
{
    m_ssdp.stop();
    // disconnect() joins the receive thread and suppresses the lost callback,
    // so no thread is left to reach back into a half-destroyed agent. Clearing
    // the callbacks afterwards is then race-free.
    m_mqtt.disconnect();
    join_connect_thread();
    m_mqtt.set_message_fn(nullptr);
    m_mqtt.set_lost_fn(nullptr);
}

AgentInfo BambuLanPrinterAgent::get_agent_info_static()
{
    AgentInfo info;
    // Same id as the plugin-backed agent: Orca selects "bbl" for every Bambu
    // vendor preset, and this agent stands in for it.
    info.id          = "bbl";
    info.name        = "Bambu Lab";
    info.version     = BAMBU_LAN_AGENT_VERSION;
    info.description = "Native Bambu LAN agent (MQTT + FTPS, no network plugin)";
    return info;
}

void BambuLanPrinterAgent::set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) { m_cloud_agent = std::move(cloud); }

void BambuLanPrinterAgent::join_connect_thread()
{
    if (m_connect_thread.joinable()) {
        if (m_connect_thread.get_id() == std::this_thread::get_id())
            m_connect_thread.detach();
        else
            m_connect_thread.join();
    }
}

void BambuLanPrinterAgent::report_connect_state(int state, const std::string& dev_id, const std::string& msg)
{
    OnLocalConnectedFn fn;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        fn = m_on_local_connect_fn;
    }
    if (fn)
        fn(state, dev_id, msg);
}

void BambuLanPrinterAgent::on_mqtt_message(const std::string& topic, const std::string& payload)
{
    // device/<serial>/report -> the serial is what Orca keys machines by.
    std::string dev_id;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        dev_id = m_dev_id;
    }
    if (topic.rfind("device/", 0) == 0) {
        const size_t slash = topic.find('/', 7);
        if (slash != std::string::npos)
            dev_id = topic.substr(7, slash - 7);
    }

    OnMessageFn fn;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        fn = m_on_local_message_fn;
    }
    if (fn)
        fn(dev_id, payload);
}

void BambuLanPrinterAgent::on_mqtt_lost(const std::string& reason)
{
    std::string dev_id;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        dev_id = m_dev_id;
    }
    BOOST_LOG_TRIVIAL(warning) << "BambuLanPrinterAgent: LAN connection lost: " << reason;
    report_connect_state(ConnectStatus::ConnectStatusLost, dev_id, reason);
}

int BambuLanPrinterAgent::connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl)
{
    if (dev_ip.empty() || dev_id.empty()) {
        BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: connect_printer without ip or serial";
        return BAMBU_NETWORK_ERR_CONNECT_FAILED;
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_dev_id   = dev_id;
        m_dev_ip   = dev_ip;
        m_username = username.empty() ? std::string("bblp") : username;
        m_password = password;
        m_use_ssl  = use_ssl;
    }

    // Connecting takes a TCP round trip plus a TLS handshake against a device
    // that may be asleep. The plugin reported the outcome through
    // set_on_local_connect_fn rather than blocking, and DeviceManager calls this
    // from the UI thread, so do the same here.
    //
    // Tearing down the previous session happens on the worker too: waiting for
    // an attempt against an unreachable printer would otherwise freeze the UI
    // for the whole connect timeout when the user switches machines mid-attempt.
    const int  generation = m_connect_generation.fetch_add(1) + 1;
    std::thread previous  = std::move(m_connect_thread);

    m_connecting.store(true);
    m_connect_thread = std::thread([this, dev_id, dev_ip, username, password, use_ssl, generation,
                                    previous = std::move(previous)]() mutable {
        if (previous.joinable())
            previous.join();
        m_mqtt.disconnect();

        // A newer connect_printer() overtook us while we waited; its own worker
        // owns the connection now and reporting here would fight with it.
        if (m_connect_generation.load() != generation)
            return;

        BambuLan::MqttConfig cfg;
        cfg.host     = dev_ip;
        // The firmware only serves MQTT behind TLS; the plaintext port is here
        // for completeness (and for the host self test), never used against a
        // real printer.
        cfg.port     = use_ssl ? 8883 : 1883;
        cfg.use_tls  = use_ssl;
        cfg.username = username.empty() ? std::string("bblp") : username;
        cfg.password = password;
        // Short enough that switching machines feels responsive, long enough
        // for a printer that is awake but busy.
        cfg.connect_timeout_s = 6;

        std::string error;
        const int   rc = m_mqtt.connect(cfg, error);
        m_connecting.store(false);

        if (m_connect_generation.load() != generation) {
            // Superseded while we were connecting: hand the socket over to
            // nobody rather than report a machine the UI has moved on from.
            if (rc == BambuLan::ConnackAccepted)
                m_mqtt.disconnect();
            return;
        }

        if (rc != BambuLan::ConnackAccepted) {
            BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: connect to " << dev_ip << " failed: " << error;
            // Orca reads this string: "5" means "wrong access code" and clears
            // the stored one. Anything else is shown verbatim as a code.
            report_connect_state(ConnectStatus::ConnectStatusFailed, dev_id, std::to_string(rc));
            return;
        }

        const std::string report_topic = "device/" + dev_id + "/report";
        if (!m_mqtt.subscribe(report_topic, 0)) {
            BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: subscribe to " << report_topic << " failed";
            GetSubscribeFailureFn fail_fn;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                fail_fn = m_on_subscribe_failure_fn;
            }
            if (fail_fn)
                fail_fn(report_topic);
            m_mqtt.disconnect();
            report_connect_state(ConnectStatus::ConnectStatusFailed, dev_id, std::to_string(BambuLan::ConnackTransportError));
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "BambuLanPrinterAgent: connected to " << dev_ip << " (" << dev_id << ")";
        OnPrinterConnectedFn connected_fn;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            connected_fn = m_on_printer_connected_fn;
        }
        if (connected_fn)
            connected_fn(report_topic);
        report_connect_state(ConnectStatus::ConnectStatusOk, dev_id, "0");
    });

    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::disconnect_printer()
{
    m_mqtt.disconnect();
    join_connect_thread();
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag)
{
    (void) flag;
    std::string target;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        target = dev_id.empty() ? m_dev_id : dev_id;
    }
    if (target.empty())
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    if (!m_mqtt.is_connected())
        return BAMBU_NETWORK_ERR_SEND_MSG_FAILED;

    const std::string topic = "device/" + target + "/request";
    if (!m_mqtt.publish(topic, json_str, static_cast<uint8_t>(qos < 0 ? 0 : (qos > 1 ? 1 : qos)))) {
        BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: publish to " << topic << " failed";
        return BAMBU_NETWORK_ERR_SEND_MSG_FAILED;
    }
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::send_message(std::string dev_id, std::string json_str, int qos, int flag)
{
    // There is no cloud relay here. When the target happens to be the printer
    // we already hold a LAN session to, deliver it over that instead of failing.
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (dev_id.empty() || dev_id != m_dev_id)
            return BAMBU_NETWORK_ERR_SEND_MSG_FAILED;
    }
    return send_message_to_printer(std::move(dev_id), std::move(json_str), qos, flag);
}

int  BambuLanPrinterAgent::check_cert() { return BAMBU_NETWORK_SUCCESS; }
void BambuLanPrinterAgent::install_device_cert(std::string dev_id, bool lan_only)
{
    // LAN mode authenticates with the access code over a self-signed TLS
    // channel; there is no device certificate to install.
    (void) dev_id;
    (void) lan_only;
}

bool BambuLanPrinterAgent::start_discovery(bool start, bool sending)
{
    if (!start) {
        m_ssdp.stop();
        return true;
    }
    if (m_ssdp.is_running())
        return true;
    const bool ok = m_ssdp.start(sending);
    if (!ok)
        BOOST_LOG_TRIVIAL(warning) << "BambuLanPrinterAgent: SSDP discovery unavailable; add printers by IP instead";
    return ok;
}

int BambuLanPrinterAgent::ping_bind(std::string ping_code)
{
    (void) ping_code;
    return BAMBU_NETWORK_ERR_BIND_FAILED;
}

int BambuLanPrinterAgent::bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect)
{
    // Binding pairs a printer with a Bambu account through the cloud; a LAN-only
    // agent has no part in it. Orca's own IP-entry dialog already takes the
    // manual path on Apple platforms.
    (void) dev_ip;
    (void) sec_link;
    (void) detect;
    return BAMBU_NETWORK_ERR_BIND_FAILED;
}

int BambuLanPrinterAgent::bind(std::string dev_ip, std::string dev_id, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn)
{
    (void) dev_ip;
    (void) dev_id;
    (void) sec_link;
    (void) timezone;
    (void) improved;
    (void) update_fn;
    return BAMBU_NETWORK_ERR_BIND_FAILED;
}

int BambuLanPrinterAgent::unbind(std::string dev_id)
{
    (void) dev_id;
    return BAMBU_NETWORK_ERR_UNBIND_FAILED;
}

int BambuLanPrinterAgent::request_bind_ticket(std::string* ticket)
{
    (void) ticket;
    return BAMBU_NETWORK_ERR_BIND_FAILED;
}

int BambuLanPrinterAgent::set_server_callback(OnServerErrFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_server_err_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

std::string BambuLanPrinterAgent::get_user_selected_machine()
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_selected_machine;
}

int BambuLanPrinterAgent::set_user_selected_machine(std::string dev_id)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_selected_machine = std::move(dev_id);
    return BAMBU_NETWORK_SUCCESS;
}

bool BambuLanPrinterAgent::ensure_connected(const PrintParams& params)
{
    if (m_mqtt.is_connected())
        return true;
    if (params.dev_ip.empty() || params.dev_id.empty())
        return false;

    BambuLan::MqttConfig cfg;
    cfg.host     = params.dev_ip;
    cfg.port     = params.use_ssl_for_mqtt ? 8883 : 1883;
    cfg.use_tls  = params.use_ssl_for_mqtt;
    cfg.username = params.username.empty() ? std::string("bblp") : params.username;
    cfg.password = params.password;

    std::string error;
    if (m_mqtt.connect(cfg, error) != BambuLan::ConnackAccepted) {
        BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: print-time connect failed: " << error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_dev_id   = params.dev_id;
        m_dev_ip   = params.dev_ip;
        m_username = cfg.username;
        m_password = cfg.password;
    }
    m_mqtt.subscribe("device/" + params.dev_id + "/report", 0);
    return true;
}

int BambuLanPrinterAgent::upload_print_file(const PrintParams& params,
                                            OnUpdateStatusFn   update_fn,
                                            WasCancelledFn     cancel_fn,
                                            std::string&       remote_name)
{
    const long long size = file_size_of(params.filename);
    if (size < 0)
        return BAMBU_NETWORK_ERR_FILE_NOT_EXIST;
    if (size > MAX_PRINT_FILE_SIZE)
        return BAMBU_NETWORK_ERR_FILE_OVER_SIZE;

    remote_name = remote_name_for(params);

    BambuLan::FtpsConfig cfg;
    cfg.host     = params.dev_ip;
    cfg.port     = 990;
    cfg.username = params.username.empty() ? std::string("bblp") : params.username;
    cfg.password = params.password;
    cfg.use_tls  = params.use_ssl_for_ftp;

    std::string error;
    const int   rc = BambuLan::ftps_upload_file(
        cfg, params.filename, remote_name,
        [update_fn](int percent) { notify(update_fn, SendingPrintJobStage::PrintingStageUpload, percent, std::to_string(percent) + "%"); },
        [cancel_fn]() { return cancelled(cancel_fn); },
        error);

    if (rc == BambuLan::FtpsOk) {
        BOOST_LOG_TRIVIAL(info) << "BambuLanPrinterAgent: uploaded " << params.filename << " as " << remote_name;
        return BAMBU_NETWORK_SUCCESS;
    }

    BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: FTPS upload failed: " << error;
    switch (rc) {
    case BambuLan::FtpsCancelled: return BAMBU_NETWORK_ERR_CANCELED;
    case BambuLan::FtpsFileNotFound: return BAMBU_NETWORK_ERR_FILE_NOT_EXIST;
    default: return BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED;
    }
}

int BambuLanPrinterAgent::publish_print_command(const PrintParams& params, const std::string& remote_name)
{
    BambuLan::LanPrintRequest req;
    req.file_name      = remote_name;
    req.ftp_folder     = params.ftp_folder;
    req.plate_index    = params.plate_index;
    req.subtask_name   = params.project_name.empty() ? params.task_name : params.project_name;
    req.bed_type       = params.task_bed_type;
    req.timelapse      = params.task_record_timelapse;
    req.bed_leveling   = params.task_bed_leveling;
    req.flow_cali      = params.task_flow_cali;
    req.vibration_cali = params.task_vibration_cali;
    req.layer_inspect  = params.task_layer_inspect;
    req.use_ams        = params.task_use_ams;
    req.ams_mapping    = params.ams_mapping;
    req.ams_mapping2   = params.ams_mapping2;
    req.sequence_id    = m_sequence_id.fetch_add(1);

    const std::string command = BambuLan::build_project_file_command(req);
    BOOST_LOG_TRIVIAL(info) << "BambuLanPrinterAgent: print command " << command;

    const std::string dev_id = params.dev_id.empty() ? get_user_selected_machine() : params.dev_id;
    return send_message_to_printer(dev_id, command, 1, 0);
}

int BambuLanPrinterAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    notify(update_fn, SendingPrintJobStage::PrintingStageCreate, 0, "");

    if (params.dev_ip.empty() || params.password.empty()) {
        BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: LAN print needs both an IP and an access code";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    if (cancelled(cancel_fn))
        return BAMBU_NETWORK_ERR_CANCELED;

    std::string remote_name;
    const int   upload_rc = upload_print_file(params, update_fn, cancel_fn, remote_name);
    if (upload_rc == BAMBU_NETWORK_ERR_CANCELED)
        return BAMBU_NETWORK_ERR_CANCELED;
    if (upload_rc == BAMBU_NETWORK_ERR_FILE_OVER_SIZE)
        return BAMBU_NETWORK_ERR_PRINT_LP_FILE_OVER_SIZE;
    if (upload_rc != BAMBU_NETWORK_SUCCESS)
        return BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED;

    if (cancelled(cancel_fn))
        return BAMBU_NETWORK_ERR_CANCELED;

    notify(update_fn, SendingPrintJobStage::PrintingStageSending, 0, "");

    if (!ensure_connected(params)) {
        BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: cannot reach the printer over MQTT to start the print";
        return BAMBU_NETWORK_ERR_PRINT_LP_PUBLISH_MSG_FAILED;
    }
    if (publish_print_command(params, remote_name) != BAMBU_NETWORK_SUCCESS)
        return BAMBU_NETWORK_ERR_PRINT_LP_PUBLISH_MSG_FAILED;

    notify(update_fn, SendingPrintJobStage::PrintingStageFinished, 0, "");
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    // "with record" means the cloud also gets a task entry. Without an account
    // there is nothing to record, so this is the plain LAN print. wait_fn polls
    // for a cloud-assigned job id that will never arrive, so it is not called.
    (void) wait_fn;
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int BambuLanPrinterAgent::start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    (void) wait_fn;
    notify(update_fn, SendingPrintJobStage::PrintingStageCreate, 0, "");

    if (params.dev_ip.empty() || params.password.empty())
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    std::string remote_name;
    const int   rc = upload_print_file(params, update_fn, cancel_fn, remote_name);
    if (rc == BAMBU_NETWORK_SUCCESS) {
        notify(update_fn, SendingPrintJobStage::PrintingStageFinished, 0, "");
        return BAMBU_NETWORK_SUCCESS;
    }
    if (rc == BAMBU_NETWORK_ERR_CANCELED)
        return rc;
    return BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED;
}

int BambuLanPrinterAgent::start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    // The file is already on the printer: only the command is needed.
    notify(update_fn, SendingPrintJobStage::PrintingStageCreate, 0, "");
    if (cancelled(cancel_fn))
        return BAMBU_NETWORK_ERR_CANCELED;

    const std::string remote_name = BambuLan::sanitize_remote_file_name(params.dst_file.empty() ? params.filename : params.dst_file);
    if (remote_name.empty())
        return BAMBU_NETWORK_ERR_FILE_NOT_EXIST;

    if (!ensure_connected(params))
        return BAMBU_NETWORK_ERR_PRINT_LP_PUBLISH_MSG_FAILED;
    if (publish_print_command(params, remote_name) != BAMBU_NETWORK_SUCCESS)
        return BAMBU_NETWORK_ERR_PRINT_LP_PUBLISH_MSG_FAILED;

    notify(update_fn, SendingPrintJobStage::PrintingStageFinished, 0, "");
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    // Cloud print. If the caller gave us LAN credentials anyway (Orca falls back
    // to this path when it thinks the cloud is available), do the LAN thing
    // rather than fail outright.
    (void) wait_fn;
    if (!params.dev_ip.empty() && !params.password.empty())
        return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));

    BOOST_LOG_TRIVIAL(error) << "BambuLanPrinterAgent: cloud printing is not available in LAN-only mode";
    notify(update_fn, SendingPrintJobStage::PrintingStageERROR, BAMBU_NETWORK_ERR_CONNECTION_TO_SERVER_FAILED, "");
    return BAMBU_NETWORK_ERR_CONNECTION_TO_SERVER_FAILED;
}

int BambuLanPrinterAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_ssdp_msg_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_printer_connected_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_subscribe_failure_fn(GetSubscribeFailureFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_subscribe_failure_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_user_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_user_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_local_connect_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_on_local_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_on_local_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int BambuLanPrinterAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_queue_on_main_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

} // namespace Slic3r
