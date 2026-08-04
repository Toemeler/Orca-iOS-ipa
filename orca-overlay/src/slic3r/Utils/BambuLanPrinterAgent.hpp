// Bambu LAN backend - IPrinterAgent implementation
// ------------------------------------------------
// Drop-in replacement for BBLPrinterAgent that speaks to the printer directly
// instead of through Bambu's closed network plugin:
//
//   * status and control : MQTT 3.1.1 over TLS, port 8883, user `bblp`,
//                          password = the LAN access code, topics
//                          device/<serial>/{report,request}
//   * sending a print    : FTPS (implicit TLS, port 990) upload followed by a
//                          `project_file` command over MQTT
//   * discovery          : SSDP announcements on UDP 1990/2021
//
// It registers under the same agent id ("bbl") as the plugin wrapper, so every
// call site above it - DeviceManager, PrintJob, the Device tab - is unchanged.
// Cloud-only operations (account binding, cloud print, cloud relay) return
// errors: this is a LAN-only agent by design.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef slic3r_BambuLanPrinterAgent_hpp_
#define slic3r_BambuLanPrinterAgent_hpp_

#include "IPrinterAgent.hpp"
#include "ICloudServiceAgent.hpp"

#include "BambuLanDiscovery.hpp"
#include "BambuLanMqtt.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Slic3r {

class BambuLanPrinterAgent : public IPrinterAgent
{
public:
    explicit BambuLanPrinterAgent(std::string log_dir);
    ~BambuLanPrinterAgent() override;

    static AgentInfo get_agent_info_static();
    AgentInfo        get_agent_info() override { return get_agent_info_static(); }

    void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) override;

    // Communication
    int send_message(std::string dev_id, std::string json_str, int qos, int flag) override;
    int connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl) override;
    int disconnect_printer() override;
    int send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) override;

    // Certificates (LAN mode authenticates with the access code; nothing to install)
    int  check_cert() override;
    void install_device_cert(std::string dev_id, bool lan_only) override;

    // Discovery
    bool start_discovery(bool start, bool sending) override;

    // Binding - cloud concepts, not available in LAN-only operation
    int ping_bind(std::string ping_code) override;
    int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) override;
    int bind(std::string dev_ip, std::string dev_id, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn) override;
    int unbind(std::string dev_id) override;
    int request_bind_ticket(std::string* ticket) override;
    int set_server_callback(OnServerErrFn fn) override;

    // Machine selection
    std::string get_user_selected_machine() override;
    int         set_user_selected_machine(std::string dev_id) override;

    // Print jobs
    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;
    int start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;

    // Callbacks
    int set_on_ssdp_msg_fn(OnMsgArrivedFn fn) override;
    int set_on_printer_connected_fn(OnPrinterConnectedFn fn) override;
    int set_on_subscribe_failure_fn(GetSubscribeFailureFn fn) override;
    int set_on_message_fn(OnMessageFn fn) override;
    int set_on_user_message_fn(OnMessageFn fn) override;
    int set_on_local_connect_fn(OnLocalConnectedFn fn) override;
    int set_on_local_message_fn(OnMessageFn fn) override;
    int set_queue_on_main_fn(QueueOnMainFn fn) override;

    // The printer pushes its whole state over MQTT, so filament data arrives
    // without anyone asking for it.
    FilamentSyncMode get_filament_sync_mode() const override { return FilamentSyncMode::subscription; }

private:
    // Uploads params.filename and returns the name it was stored under.
    int  upload_print_file(const PrintParams& params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string& remote_name);
    int  publish_print_command(const PrintParams& params, const std::string& remote_name);
    // Connects if needed so a print can be sent to a printer that is not the
    // currently selected one.
    bool ensure_connected(const PrintParams& params);
    void join_connect_thread();
    void on_mqtt_message(const std::string& topic, const std::string& payload);
    void on_mqtt_lost(const std::string& reason);
    void report_connect_state(int state, const std::string& dev_id, const std::string& msg);

    std::shared_ptr<ICloudServiceAgent> m_cloud_agent;
    std::string                         m_log_dir;

    BambuLan::MqttClient   m_mqtt;
    BambuLan::SsdpListener m_ssdp;

    mutable std::mutex m_state_mutex;
    std::string        m_dev_id;
    std::string        m_dev_ip;
    std::string        m_username;
    std::string        m_password;
    bool               m_use_ssl = true;
    std::string        m_selected_machine;

    std::thread       m_connect_thread;
    std::atomic<bool> m_connecting{false};
    std::atomic<int>  m_sequence_id{0};

    OnMsgArrivedFn       m_on_ssdp_msg_fn;
    OnPrinterConnectedFn m_on_printer_connected_fn;
    GetSubscribeFailureFn m_on_subscribe_failure_fn;
    OnMessageFn          m_on_message_fn;
    OnMessageFn          m_on_user_message_fn;
    OnLocalConnectedFn   m_on_local_connect_fn;
    OnMessageFn          m_on_local_message_fn;
    QueueOnMainFn        m_queue_on_main_fn;
    OnServerErrFn        m_on_server_err_fn;
};

} // namespace Slic3r

#endif // slic3r_BambuLanPrinterAgent_hpp_
