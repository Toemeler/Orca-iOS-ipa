// Host-side self test for the Bambu LAN backend.
//
// Links the same BambuLanMqtt / BambuLanFtps / BambuLanDiscovery /
// BambuLanPrintCommand sources that go into the app and exercises them:
//
//   1. codec unit tests (framing, varints, partial reads, malformed input)
//   2. print-command and URL construction
//   3. SSDP announcement parsing
//   4. a live round trip against tools/bambu-lan/mock_printer.py - TLS MQTT
//      connect, wrong-password rejection, subscribe, receive a pushed report,
//      publish, keepalive, then an implicit-FTPS upload
//
// Build and run through tools/bambu-lan/run-selftest.sh.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanDiscovery.hpp"
#include "BambuLanFtps.hpp"
#include "BambuLanMqtt.hpp"
#include "BambuLanPrintCommand.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Slic3r::BambuLan;
using json = nlohmann::json;

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what)
{
    ++g_checks;
    if (ok) {
        std::cout << "  ok   " << what << "\n";
    } else {
        ++g_failures;
        std::cout << "  FAIL " << what << "\n";
    }
}

void check_eq(const std::string& got, const std::string& want, const std::string& what)
{
    check(got == want, what + (got == want ? "" : "  (got \"" + got + "\", want \"" + want + "\")"));
}

std::string hex(const std::vector<uint8_t>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string        out;
    for (uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool wait_for(const std::function<bool()>& pred, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return pred();
}

// ---------------------------------------------------------------------------

void test_varint()
{
    std::cout << "varint\n";
    struct Case { uint32_t value; const char* encoded; };
    const Case cases[] = {
        {0, "00"}, {127, "7f"}, {128, "8001"}, {16383, "ff7f"}, {16384, "808001"}, {2097151, "ffff7f"},
        {2097152, "80808001"}, {268435455, "ffffff7f"},
    };
    for (const Case& c : cases) {
        std::vector<uint8_t> out;
        encode_varint(out, c.value);
        check_eq(hex(out), c.encoded, "encode " + std::to_string(c.value));

        uint32_t decoded = 0;
        size_t   used    = 0;
        check(decode_varint(out.data(), out.size(), decoded, used) == VarintResult::Ok && decoded == c.value
                  && used == out.size(),
              "round trip " + std::to_string(c.value));

        // Every proper prefix must report NeedMore rather than a wrong value.
        bool prefixes_ok = true;
        for (size_t n = 0; n < out.size(); ++n)
            prefixes_ok &= decode_varint(out.data(), n, decoded, used) == VarintResult::NeedMore;
        check(prefixes_ok, "truncated prefixes of " + std::to_string(c.value) + " report NeedMore");
    }

    uint32_t      value = 0;
    size_t        used  = 0;
    const uint8_t bad[] = {0xff, 0xff, 0xff, 0xff, 0x7f};
    check(decode_varint(bad, sizeof(bad), value, used) == VarintResult::Malformed, "5-byte length is malformed");
}

void test_connect_encoding()
{
    std::cout << "CONNECT encoding\n";
    const std::vector<uint8_t> pkt = encode_connect("orcaios0123", "bblp", "12345678", 30);

    // 10 (variable header) + 2+11 + 2+4 + 2+8 = 39 bytes of body.
    check(pkt.size() == 2 + 39, "packet length");
    check(pkt[0] == 0x10, "fixed header is CONNECT with zero flags");
    check(pkt[1] == 39, "remaining length");
    check(std::memcmp(&pkt[2], "\x00\x04MQTT", 6) == 0, "protocol name");
    check(pkt[8] == 0x04, "protocol level 4 (3.1.1)");
    check(pkt[9] == (0x80 | 0x40 | 0x02), "user + password + clean-session flags");
    check(pkt[10] == 0 && pkt[11] == 30, "keepalive 30s");

    const std::vector<uint8_t> anon = encode_connect("id", "", "", 60);
    check(anon[9] == 0x02, "no credentials -> clean session only");
}

void test_publish_subscribe_encoding()
{
    std::cout << "PUBLISH / SUBSCRIBE encoding\n";
    const std::vector<uint8_t> pub0 = encode_publish("device/X/request", "{}", 0, 7);
    check(pub0[0] == 0x30, "qos 0 publish header");
    Packet p;
    size_t consumed = 0;
    check(decode_packet(pub0.data(), pub0.size(), p, consumed) == DecodeResult::Ok, "qos 0 decodes");
    check_eq(p.topic, "device/X/request", "qos 0 topic");
    check_eq(p.payload, "{}", "qos 0 payload");
    check(consumed == pub0.size(), "qos 0 consumed everything");

    const std::vector<uint8_t> pub1 = encode_publish("t", "hello", 1, 0x1234);
    check(pub1[0] == 0x32, "qos 1 publish header");
    check(decode_packet(pub1.data(), pub1.size(), p, consumed) == DecodeResult::Ok, "qos 1 decodes");
    check(p.packet_id == 0x1234, "qos 1 packet id survives");
    check_eq(p.payload, "hello", "qos 1 payload");

    const std::vector<uint8_t> sub = encode_subscribe(9, "device/X/report", 0);
    check(sub[0] == 0x82, "SUBSCRIBE reserved flags are 0010");
}

void test_decode_stream()
{
    std::cout << "stream decoding\n";
    // Three packets in one buffer, the way a single TLS record delivers them.
    std::vector<uint8_t> stream;
    const std::vector<uint8_t> a = encode_publish("t1", "one", 0, 0);
    const std::vector<uint8_t> b = encode_publish("t2", std::string(300, 'x'), 0, 0); // 2-byte length
    const std::vector<uint8_t> c = encode_pingreq();
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());
    stream.insert(stream.end(), c.begin(), c.end());

    size_t   offset = 0;
    Packet   p;
    size_t   consumed = 0;
    check(decode_packet(stream.data(), stream.size(), p, consumed) == DecodeResult::Ok && p.topic == "t1", "first packet");
    offset += consumed;
    check(decode_packet(stream.data() + offset, stream.size() - offset, p, consumed) == DecodeResult::Ok
              && p.payload.size() == 300,
          "second packet spans a 2-byte remaining length");
    offset += consumed;
    check(decode_packet(stream.data() + offset, stream.size() - offset, p, consumed) == DecodeResult::Ok
              && p.type == PacketType::Pingreq,
          "third packet");
    offset += consumed;
    check(offset == stream.size(), "stream fully consumed");

    // Every truncation of the middle packet has to say NeedMore, never Ok.
    bool truncations_ok = true;
    for (size_t n = 1; n < b.size(); ++n)
        truncations_ok &= decode_packet(b.data(), n, p, consumed) == DecodeResult::NeedMore;
    check(truncations_ok, "partial packets report NeedMore");

    const uint8_t garbage[] = {0x00, 0x00};
    check(decode_packet(garbage, sizeof(garbage), p, consumed) == DecodeResult::Malformed, "packet type 0 is malformed");

    const std::vector<uint8_t> connack = {0x20, 0x02, 0x00, 0x05};
    check(decode_packet(connack.data(), connack.size(), p, consumed) == DecodeResult::Ok
              && p.type == PacketType::Connack && p.return_code == 5,
          "CONNACK 5 (bad access code) decodes");
}

void test_print_command()
{
    std::cout << "print command\n";
    check_eq(build_print_url("sdcard/", "cube.gcode.3mf"), "file:///sdcard/cube.gcode.3mf", "A1/P1 url");
    check_eq(build_print_url("", "cube.gcode.3mf"), "file:///sdcard/cube.gcode.3mf", "empty ftp_folder falls back");
    check_eq(build_print_url("/sdcard", "a.3mf"), "file:///sdcard/a.3mf", "slashes normalised");

    check_eq(sanitize_remote_file_name("/tmp/orca/My Print.gcode.3mf"), "My Print.gcode.3mf", "basename kept");
    check_eq(sanitize_remote_file_name("a\"b:c*.3mf"), "a_b_c_.3mf", "hostile characters replaced");

    LanPrintRequest req;
    req.file_name    = "/tmp/whatever/cube.gcode.3mf";
    req.ftp_folder   = "sdcard/";
    req.plate_index  = 2;
    req.subtask_name = "cube";
    req.bed_type     = "textured_plate";
    req.use_ams      = true;
    req.ams_mapping  = "[0,-1]";
    req.sequence_id  = 3;

    const json j = json::parse(build_project_file_command(req));
    check(j.contains("print"), "wrapped in a print object");
    const json& p = j["print"];
    check_eq(p["command"].get<std::string>(), "project_file", "command");
    check_eq(p["param"].get<std::string>(), "Metadata/plate_2.gcode", "plate index -> param");
    check_eq(p["url"].get<std::string>(), "file:///sdcard/cube.gcode.3mf", "url uses the basename only");
    check_eq(p["subtask_name"].get<std::string>(), "cube", "subtask name");
    check_eq(p["bed_type"].get<std::string>(), "textured_plate", "bed type passed through");
    check_eq(p["sequence_id"].get<std::string>(), "3", "sequence id is a string");
    check(p["use_ams"].get<bool>(), "use_ams");
    check(p["ams_mapping"].is_array() && p["ams_mapping"].size() == 2, "ams_mapping parsed into an array");
    check(p.contains("bed_leveling") && p.contains("bed_levelling"), "both spellings of bed levelling are sent");
    check(p["project_id"] == "0" && p["task_id"] == "0" && p["subtask_id"] == "0", "cloud ids zeroed for LAN");

    LanPrintRequest bad;
    bad.file_name   = "x.3mf";
    bad.ams_mapping = "not json";
    const json j2   = json::parse(build_project_file_command(bad));
    check(!j2["print"].contains("ams_mapping"), "unparsable ams mapping is dropped, not forwarded");
    check_eq(j2["print"]["subtask_name"].get<std::string>(), "x", "subtask name defaults to the file stem");
    check_eq(j2["print"]["param"].get<std::string>(), "Metadata/plate_1.gcode", "plate defaults to 1");
}

void test_ssdp_parsing()
{
    std::cout << "SSDP parsing\n";
    const std::string notify =
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1990\r\n"
        "Server: UPnP/1.0\r\n"
        "Location: 192.168.1.42\r\n"
        "NT: urn:bambulab-com:device:3dprinter:1\r\n"
        "USN: 00M09A351100999\r\n"
        "DevModel.bambu.com: N2S\r\n"
        "DevName.bambu.com: My A1\r\n"
        "DevSignal.bambu.com: -44\r\n"
        "DevConnect.bambu.com: lan\r\n"
        "DevBind.bambu.com: free\r\n"
        "Devseclink.bambu.com: secure\r\n"
        "DevVersion.bambu.com: 00.00.00.01\r\n"
        "\r\n";

    DiscoveredPrinter printer;
    check(parse_ssdp_announcement(notify, printer), "bambu announcement recognised");
    check_eq(printer.dev_id, "00M09A351100999", "serial");
    check_eq(printer.dev_ip, "192.168.1.42", "ip");
    check_eq(printer.dev_type, "N2S", "model");
    check_eq(printer.dev_name, "My A1", "name");
    check_eq(printer.connect_type, "lan", "connect type");

    const json j = json::parse(to_machine_alive_json(printer));
    // These are exactly the keys DeviceManager::on_machine_alive() reads.
    for (const char* key : {"dev_name", "dev_id", "dev_ip", "dev_type", "dev_signal", "connect_type", "bind_state"})
        check(j.contains(key), std::string("machine-alive json has ") + key);

    DiscoveredPrinter other;
    check(!parse_ssdp_announcement("NOTIFY * HTTP/1.1\r\nNT: urn:schemas-upnp-org:device:MediaServer:1\r\n"
                                   "USN: uuid:1\r\nLocation: http://1.2.3.4/\r\n\r\n",
                                   other),
          "non-bambu announcement ignored");
    check(!parse_ssdp_announcement("M-SEARCH * HTTP/1.1\r\nST: urn:bambulab-com:device:3dprinter:1\r\n"
                                   "USN: x\r\nLocation: y\r\n\r\n",
                                   other),
          "our own M-SEARCH is not mistaken for a printer");
}

// ---------------------------------------------------------------------------
// Live round trip against the mock printer
// ---------------------------------------------------------------------------

void test_live(const std::string& host, uint16_t mqtt_port, uint16_t ftp_port, const std::string& access_code,
               const std::string& serial, const std::string& state_dir)
{
    std::cout << "live: wrong access code\n";
    {
        MqttClient  client;
        MqttConfig  cfg;
        cfg.host     = host;
        cfg.port     = mqtt_port;
        cfg.password = "00000000";
        std::string error;
        const int   rc = client.connect(cfg, error);
        check(rc == ConnackNotAuthorized, "CONNACK 5 surfaces as ConnackNotAuthorized (got " + std::to_string(rc) + ")");
        check(!client.is_connected(), "client stays disconnected");
    }

    std::cout << "live: connect, subscribe, receive, publish\n";
    MqttClient client;

    std::mutex               mutex;
    std::vector<std::string> received;
    std::atomic<bool>        lost{false};
    client.set_message_fn([&](const std::string& topic, const std::string& payload) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(topic + "|" + payload);
    });
    client.set_lost_fn([&](const std::string&) { lost.store(true); });

    MqttConfig cfg;
    cfg.host        = host;
    cfg.port        = mqtt_port;
    cfg.password    = access_code;
    cfg.keepalive_s = 2; // so the keepalive fires inside the test's lifetime

    std::string error;
    const int   rc = client.connect(cfg, error);
    check(rc == ConnackAccepted, "connect accepted" + std::string(rc == ConnackAccepted ? "" : ": " + error));
    if (rc != ConnackAccepted)
        return;

    check(client.subscribe("device/" + serial + "/report", 0), "subscribe sent");
    check(wait_for([&] { std::lock_guard<std::mutex> lock(mutex); return !received.empty(); }, 5000),
          "pushed report received");
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!received.empty()) {
            const std::string& first = received.front();
            check(first.rfind("device/" + serial + "/report|", 0) == 0, "report arrived on the expected topic");
            const json payload = json::parse(first.substr(first.find('|') + 1), nullptr, false);
            check(!payload.is_discarded() && payload.contains("print"), "report payload is the printer's json");
        }
    }

    LanPrintRequest req;
    req.file_name  = "selftest.gcode.3mf";
    req.ftp_folder = "sdcard/";
    check(client.publish("device/" + serial + "/request", build_project_file_command(req), 1), "publish (qos 1) sent");

    // Two keepalive periods: proves PINGREQ goes out and PINGRESP keeps the
    // link from being declared dead.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    check(!lost.load(), "connection still alive after a keepalive round");
    check(client.is_connected(), "client still reports connected");

    client.disconnect();
    check(!client.is_connected(), "disconnect clears the connected flag");

    std::cout << "live: FTPS upload\n";
    const std::string local = state_dir + "/payload.bin";
    {
        std::ofstream out(local, std::ios::binary);
        for (int i = 0; i < 40000; ++i)
            out.put(static_cast<char>('A' + (i % 26)));
    }

    FtpsConfig ftps;
    ftps.host     = host;
    ftps.port     = ftp_port;
    ftps.password = access_code;

    std::atomic<int> last_percent{-1};
    std::string      ftp_error;
    const int        up = ftps_upload_file(
        ftps, local, "selftest.gcode.3mf", [&](int percent) { last_percent.store(percent); }, nullptr, ftp_error);
    check(up == FtpsOk, "upload succeeded" + std::string(up == FtpsOk ? "" : ": " + ftp_error));
    check(last_percent.load() == 100, "progress reached 100%");

    const std::string uploaded = read_file(state_dir + "/uploads/selftest.gcode.3mf");
    check(uploaded.size() == 40000, "uploaded byte count matches (" + std::to_string(uploaded.size()) + ")");
    check(uploaded == read_file(local), "uploaded bytes match the source exactly");

    std::string bad_error;
    FtpsConfig  bad = ftps;
    bad.password    = "00000000";
    const int bad_rc = ftps_upload_file(bad, local, "nope.3mf", nullptr, nullptr, bad_error);
    check(bad_rc == FtpsAuthFailed, "wrong access code is reported as an auth failure (got " + std::to_string(bad_rc) + ")");

    std::cout << "live: what the printer actually received\n";
    const std::string connects = read_file(state_dir + "/mqtt_connect.jsonl");
    check(connects.find("\"protocol\": \"MQTT\"") != std::string::npos, "printer saw a well-formed MQTT CONNECT");
    check(connects.find("\"level\": 4") != std::string::npos, "protocol level 4");
    check(connects.find("\"username\": \"bblp\"") != std::string::npos, "username bblp");

    const std::string subs = read_file(state_dir + "/mqtt_subscribe.jsonl");
    check(subs.find("device/" + serial + "/report") != std::string::npos, "printer saw the report subscription");

    const std::string published = read_file(state_dir + "/mqtt_published.jsonl");
    check(published.find("device/" + serial + "/request") != std::string::npos, "printer saw the request topic");
    check(published.find("project_file") != std::string::npos, "printer saw the project_file command");

    check(!read_file(state_dir + "/mqtt_ping.jsonl").empty(), "printer saw a keepalive PINGREQ");
    check(!read_file(state_dir + "/mqtt_disconnect.jsonl").empty(), "printer saw a clean DISCONNECT");
}

// The client has to survive a printer that goes away and comes back on the
// same object: assigning over a finished-but-joinable receive thread would
// call std::terminate.
void test_reconnect(const std::string& host, uint16_t mqtt_port, const std::string& access_code, const std::string& serial)
{
    std::cout << "live: reconnect on the same client\n";
    MqttClient client;

    std::atomic<int> lost_count{0};
    client.set_lost_fn([&](const std::string&) { lost_count.fetch_add(1); });

    MqttConfig cfg;
    cfg.host        = host;
    cfg.port        = mqtt_port;
    cfg.password    = access_code;
    cfg.keepalive_s = 10;

    std::string error;
    check(client.connect(cfg, error) == ConnackAccepted, "first connect");
    client.disconnect();
    check(lost_count.load() == 0, "a deliberate disconnect is not reported as a lost connection");

    check(client.connect(cfg, error) == ConnackAccepted, "reconnect after disconnect");

    // Now make the printer hang up on us and reconnect from the lost callback's
    // aftermath - the case that used to leave a joinable thread behind.
    check(client.publish("device/" + serial + "/request", "{\"__mock_drop__\":true}", 0), "drop request sent");
    check(wait_for([&] { return lost_count.load() > 0; }, 5000), "loss of connection is reported");
    check(!client.is_connected(), "client knows it is disconnected");

    check(client.connect(cfg, error) == ConnackAccepted, "reconnect after an unexpected drop");
    check(client.is_connected(), "connected again");
    client.disconnect();
}

void test_discovery_live(uint16_t ssdp_port)
{
    std::cout << "live: SSDP discovery\n";
    (void) ssdp_port; // the listener always binds the standard ports

    SsdpListener listener;
    std::mutex   mutex;
    std::string  seen;
    listener.set_printer_fn([&](const std::string& j) {
        std::lock_guard<std::mutex> lock(mutex);
        if (seen.empty())
            seen = j;
    });

    if (!listener.start(false)) {
        std::cout << "  skip SSDP listener (cannot bind 1990/2021 here)\n";
        return;
    }
    const bool got = wait_for([&] { std::lock_guard<std::mutex> lock(mutex); return !seen.empty(); }, 5000);
    check(got, "announcement from the mock printer received");
    if (got) {
        const json j = json::parse(seen, nullptr, false);
        check(!j.is_discarded() && j["dev_type"] == "N2S", "parsed into machine-alive json");
    }
    listener.stop();
    check(!listener.is_running(), "listener stops cleanly");
}

} // namespace

int main(int argc, char** argv)
{
    std::string host        = "127.0.0.1";
    uint16_t    mqtt_port   = 8883;
    uint16_t    ftp_port    = 990;
    uint16_t    ssdp_port   = 2021;
    std::string access_code = "12345678";
    std::string serial      = "00M09A351100999";
    std::string state_dir;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto              next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--host") host = next();
        else if (arg == "--mqtt-port") mqtt_port = static_cast<uint16_t>(std::stoi(next()));
        else if (arg == "--ftp-port") ftp_port = static_cast<uint16_t>(std::stoi(next()));
        else if (arg == "--ssdp-port") ssdp_port = static_cast<uint16_t>(std::stoi(next()));
        else if (arg == "--access-code") access_code = next();
        else if (arg == "--serial") serial = next();
        else if (arg == "--state-dir") state_dir = next();
    }

    test_varint();
    test_connect_encoding();
    test_publish_subscribe_encoding();
    test_decode_stream();
    test_print_command();
    test_ssdp_parsing();

    if (!state_dir.empty()) {
        test_live(host, mqtt_port, ftp_port, access_code, serial, state_dir);
        test_reconnect(host, mqtt_port, access_code, serial);
        test_discovery_live(ssdp_port);
    } else {
        std::cout << "(no --state-dir: offline tests only)\n";
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
