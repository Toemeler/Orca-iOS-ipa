// Bambu LAN backend - SSDP discovery. See BambuLanDiscovery.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socklen_t = int;
    #define BL_POLL WSAPoll
    #define BL_CLOSE closesocket
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define BL_POLL ::poll
    #define BL_CLOSE ::close
#endif

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace BambuLan {

using json = nlohmann::json;

namespace {

// Bambu printers announce on both of these; Bambu Studio listens on both too.
constexpr uint16_t SSDP_PORTS[]     = {2021, 1990};
constexpr char     SSDP_MULTICAST[] = "239.255.255.250";

std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string header_or(const std::map<std::string, std::string>& headers, const char* key, const std::string& fallback = std::string())
{
    auto it = headers.find(key);
    return it == headers.end() ? fallback : it->second;
}

} // namespace

std::map<std::string, std::string> parse_ssdp_headers(const std::string& datagram)
{
    std::map<std::string, std::string> headers;
    size_t                             pos = 0;
    bool                               first_line = true;

    while (pos < datagram.size()) {
        size_t eol = datagram.find('\n', pos);
        if (eol == std::string::npos)
            eol = datagram.size();
        std::string line = datagram.substr(pos, eol - pos);
        pos              = eol + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (first_line) {
            // "NOTIFY * HTTP/1.1" or "HTTP/1.1 200 OK"
            headers["__start_line__"] = trim(line);
            first_line               = false;
            continue;
        }
        if (line.empty())
            continue;

        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return headers;
}

bool parse_ssdp_announcement(const std::string& datagram, DiscoveredPrinter& out)
{
    const std::map<std::string, std::string> headers = parse_ssdp_headers(datagram);

    const std::string start_line = header_or(headers, "__start_line__");
    const std::string nt         = header_or(headers, "nt");
    const std::string st         = header_or(headers, "st");
    const std::string usn        = header_or(headers, "usn");
    const std::string location   = header_or(headers, "location");

    // Our own M-SEARCH probes come back to us on the same socket; ignore them.
    if (start_line.rfind("M-SEARCH", 0) == 0)
        return false;

    const std::string service = nt.empty() ? st : nt;
    if (service.find("bambulab") == std::string::npos && header_or(headers, "devmodel.bambu.com").empty())
        return false;
    if (usn.empty() || location.empty())
        return false;

    out                 = DiscoveredPrinter();
    out.dev_id          = usn;
    out.dev_ip          = location;
    out.dev_name        = header_or(headers, "devname.bambu.com", usn);
    out.dev_type        = header_or(headers, "devmodel.bambu.com");
    out.dev_signal      = header_or(headers, "devsignal.bambu.com");
    out.connect_type    = header_or(headers, "devconnect.bambu.com", "lan");
    out.bind_state      = header_or(headers, "devbind.bambu.com", "free");
    out.sec_link        = header_or(headers, "devseclink.bambu.com");
    out.ssdp_version    = header_or(headers, "devversion.bambu.com");
    out.connection_name = header_or(headers, "devconnectionname.bambu.com");

    // A printer that has not been set up yet reports neither, and
    // DeviceManager treats an empty bind state as "occupied".
    if (out.bind_state.empty())
        out.bind_state = "free";
    return true;
}

std::string to_machine_alive_json(const DiscoveredPrinter& printer)
{
    json j;
    j["dev_name"]     = printer.dev_name;
    j["dev_id"]       = printer.dev_id;
    j["dev_ip"]       = printer.dev_ip;
    j["dev_type"]     = printer.dev_type;
    j["dev_signal"]   = printer.dev_signal;
    j["connect_type"] = printer.connect_type;
    j["bind_state"]   = printer.bind_state;
    if (!printer.sec_link.empty())
        j["sec_link"] = printer.sec_link;
    if (!printer.ssdp_version.empty())
        j["ssdp_version"] = printer.ssdp_version;
    if (!printer.connection_name.empty())
        j["connection_name"] = printer.connection_name;
    return j.dump();
}

SsdpListener::~SsdpListener() { stop(); }

bool SsdpListener::start(bool send_search)
{
    if (m_running.load())
        return true;

    m_stop.store(false);
    m_send_search = send_search;

    for (uint16_t port : SSDP_PORTS) {
        const int fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
        if (fd < 0)
            continue;

        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_REUSEPORT
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
        ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(port);
        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            BL_CLOSE(fd);
            continue;
        }

        // Best effort: on iOS this needs the multicast entitlement, and a
        // sideloaded build does not have one. Broadcast announcements still
        // arrive on the bound port when it fails.
        struct ip_mreq mreq;
        std::memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq));

#ifndef _WIN32
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
        m_sockets.push_back(fd);
    }

    if (m_sockets.empty())
        return false;

    m_running.store(true);
    m_thread = std::thread(&SsdpListener::loop, this);
    return true;
}

void SsdpListener::stop()
{
    if (!m_running.exchange(false)) {
        // Not running, but a failed start may still have left sockets around.
        for (int fd : m_sockets)
            BL_CLOSE(fd);
        m_sockets.clear();
        return;
    }
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
    for (int fd : m_sockets)
        BL_CLOSE(fd);
    m_sockets.clear();
}

void SsdpListener::send_msearch()
{
    static const char* kSearch = "M-SEARCH * HTTP/1.1\r\n"
                                 "HOST: 239.255.255.250:1990\r\n"
                                 "MAN: \"ssdp:discover\"\r\n"
                                 "MX: 1\r\n"
                                 "ST: urn:bambulab-com:device:3dprinter:1\r\n"
                                 "\r\n";
    const size_t len = std::strlen(kSearch);

    for (int fd : m_sockets) {
        for (uint16_t port : SSDP_PORTS) {
            struct sockaddr_in dst;
            std::memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port   = htons(port);

            dst.sin_addr.s_addr = inet_addr(SSDP_MULTICAST);
            ::sendto(fd, kSearch, static_cast<int>(len), 0, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));

            dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            ::sendto(fd, kSearch, static_cast<int>(len), 0, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
        }
    }
}

void SsdpListener::loop()
{
    using namespace std::chrono;
    steady_clock::time_point next_search = steady_clock::now();

    std::vector<struct pollfd> pfds;
    pfds.reserve(m_sockets.size());

    while (!m_stop.load()) {
        if (m_send_search && steady_clock::now() >= next_search) {
            send_msearch();
            next_search = steady_clock::now() + seconds(10);
        }

        pfds.clear();
        for (int fd : m_sockets) {
            struct pollfd pfd;
            pfd.fd      = fd;
            pfd.events  = POLLIN;
            pfd.revents = 0;
            pfds.push_back(pfd);
        }
        if (BL_POLL(pfds.data(), static_cast<unsigned>(pfds.size()), 500) <= 0)
            continue;

        for (const struct pollfd& pfd : pfds) {
            if ((pfd.revents & POLLIN) == 0)
                continue;
            char               buf[2048];
            struct sockaddr_in from;
            socklen_t          from_len = sizeof(from);
            const int          n = static_cast<int>(::recvfrom(pfd.fd, buf, sizeof(buf) - 1, 0,
                                                               reinterpret_cast<struct sockaddr*>(&from), &from_len));
            if (n <= 0)
                continue;
            buf[n] = '\0';

            DiscoveredPrinter printer;
            if (!parse_ssdp_announcement(std::string(buf, static_cast<size_t>(n)), printer))
                continue;
            if (printer.dev_ip.empty()) {
                char ip[INET_ADDRSTRLEN] = {0};
                if (inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip)) != nullptr)
                    printer.dev_ip = ip;
            }
            if (m_printer_fn)
                m_printer_fn(to_machine_alive_json(printer));
        }
    }
}

} // namespace BambuLan
} // namespace Slic3r
