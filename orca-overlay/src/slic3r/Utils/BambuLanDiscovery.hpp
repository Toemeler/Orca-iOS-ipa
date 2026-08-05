// Bambu LAN backend - SSDP discovery
// ----------------------------------
// Bambu printers announce themselves with SSDP NOTIFY datagrams carrying
// Bambu-specific headers (DevModel/DevName/DevSignal/DevConnect/DevBind...).
// The closed plugin used to listen for those; this does the same and hands
// Orca's DeviceManager the JSON shape its on_machine_alive() expects.
//
// Discovery is a convenience, not a requirement: a printer added by hand
// (IP + access code) works without it, which matters on iOS where joining a
// multicast group needs an entitlement a sideloaded build does not have.
// Failures to join are therefore soft - the listener keeps running so that
// broadcast announcements still land.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef slic3r_BambuLanDiscovery_hpp_
#define slic3r_BambuLanDiscovery_hpp_

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace Slic3r {
namespace BambuLan {

// Parsed SSDP announcement, in Orca's DeviceManager vocabulary.
struct DiscoveredPrinter
{
    std::string dev_name;
    std::string dev_id;       // serial number (SSDP USN)
    std::string dev_ip;
    std::string dev_type;     // model id, e.g. "N2S"
    std::string dev_signal;   // Wi-Fi RSSI as reported, e.g. "-44"
    std::string connect_type; // "lan" or "cloud"
    std::string bind_state;   // "free" or "occupied"
    std::string sec_link;
    std::string ssdp_version;
    std::string connection_name;
};

// Splits an SSDP datagram into header name -> value (names lower-cased).
std::map<std::string, std::string> parse_ssdp_headers(const std::string& datagram);

// Returns false when the datagram is not a Bambu printer announcement.
bool parse_ssdp_announcement(const std::string& datagram, DiscoveredPrinter& out);

// The JSON DeviceManager::on_machine_alive() parses.
std::string to_machine_alive_json(const DiscoveredPrinter& printer);

class SsdpListener
{
public:
    using PrinterFn = std::function<void(const std::string& machine_alive_json)>;

    SsdpListener() = default;
    ~SsdpListener();

    SsdpListener(const SsdpListener&) = delete;
    SsdpListener& operator=(const SsdpListener&) = delete;

    void set_printer_fn(PrinterFn fn) { m_printer_fn = std::move(fn); }

    // `send_search` also emits periodic M-SEARCH probes; passive listening
    // alone is enough for printers that announce on their own schedule.
    bool start(bool send_search);
    void stop();
    bool is_running() const { return m_running.load(); }

private:
    void loop();
    void send_msearch();

    std::vector<int>  m_sockets;
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    bool              m_send_search = true;
    PrinterFn         m_printer_fn;
};

} // namespace BambuLan
} // namespace Slic3r

#endif // slic3r_BambuLanDiscovery_hpp_
