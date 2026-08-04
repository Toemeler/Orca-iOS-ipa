// Bambu LAN backend - MQTT 3.1.1 client over TLS. See BambuLanMqtt.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanMqtt.hpp"

#include <chrono>
#include <cstring>
#include <random>
#include <sstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socklen_t = int;
    #define BL_POLL WSAPoll
    #define BL_CLOSE closesocket
    #define BL_INVALID_SOCKET INVALID_SOCKET
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define BL_POLL ::poll
    #define BL_CLOSE ::close
    #define BL_INVALID_SOCKET (-1)
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace Slic3r {
namespace BambuLan {

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

void encode_varint(std::vector<uint8_t>& out, uint32_t value)
{
    do {
        uint8_t byte = static_cast<uint8_t>(value % 128);
        value /= 128;
        if (value > 0)
            byte |= 0x80;
        out.push_back(byte);
    } while (value > 0);
}

VarintResult decode_varint(const uint8_t* data, size_t len, uint32_t& value, size_t& bytes_used)
{
    uint32_t multiplier = 1;
    uint32_t result     = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (i >= len)
            return VarintResult::NeedMore;
        const uint8_t byte = data[i];
        result += static_cast<uint32_t>(byte & 0x7f) * multiplier;
        if ((byte & 0x80) == 0) {
            value      = result;
            bytes_used = i + 1;
            return VarintResult::Ok;
        }
        multiplier *= 128;
    }
    // A fifth continuation byte is out of spec (max remaining length is 4 bytes).
    return VarintResult::Malformed;
}

static void push_u16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

static void push_string(std::vector<uint8_t>& out, const std::string& s)
{
    push_u16(out, static_cast<uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

static std::vector<uint8_t> frame(uint8_t first_byte, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> out;
    out.reserve(body.size() + 5);
    out.push_back(first_byte);
    encode_varint(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> encode_connect(const std::string& client_id,
                                    const std::string& username,
                                    const std::string& password,
                                    uint16_t           keepalive_s)
{
    std::vector<uint8_t> body;
    push_string(body, "MQTT");
    body.push_back(0x04); // protocol level 4 == MQTT 3.1.1

    uint8_t flags = 0x02; // clean session
    if (!username.empty())
        flags |= 0x80;
    if (!password.empty())
        flags |= 0x40;
    body.push_back(flags);
    push_u16(body, keepalive_s);

    push_string(body, client_id);
    if (!username.empty())
        push_string(body, username);
    if (!password.empty())
        push_string(body, password);

    return frame(static_cast<uint8_t>(PacketType::Connect) << 4, body);
}

std::vector<uint8_t> encode_subscribe(uint16_t packet_id, const std::string& topic, uint8_t qos)
{
    std::vector<uint8_t> body;
    push_u16(body, packet_id);
    push_string(body, topic);
    body.push_back(static_cast<uint8_t>(qos & 0x03));
    // SUBSCRIBE carries reserved flags 0010 in the fixed header.
    return frame((static_cast<uint8_t>(PacketType::Subscribe) << 4) | 0x02, body);
}

std::vector<uint8_t> encode_publish(const std::string& topic, const std::string& payload, uint8_t qos, uint16_t packet_id)
{
    qos = static_cast<uint8_t>(qos & 0x03);
    std::vector<uint8_t> body;
    push_string(body, topic);
    if (qos > 0)
        push_u16(body, packet_id);
    body.insert(body.end(), payload.begin(), payload.end());
    return frame(static_cast<uint8_t>((static_cast<uint8_t>(PacketType::Publish) << 4) | (qos << 1)), body);
}

std::vector<uint8_t> encode_puback(uint16_t packet_id)
{
    std::vector<uint8_t> body;
    push_u16(body, packet_id);
    return frame(static_cast<uint8_t>(PacketType::Puback) << 4, body);
}

std::vector<uint8_t> encode_pingreq() { return frame(static_cast<uint8_t>(PacketType::Pingreq) << 4, {}); }

std::vector<uint8_t> encode_disconnect() { return frame(static_cast<uint8_t>(PacketType::Disconnect) << 4, {}); }

DecodeResult decode_packet(const uint8_t* buf, size_t len, Packet& out, size_t& consumed)
{
    if (len < 2)
        return DecodeResult::NeedMore;

    const uint8_t type_nibble = static_cast<uint8_t>(buf[0] >> 4);
    if (type_nibble < 1 || type_nibble > 14)
        return DecodeResult::Malformed;

    uint32_t remaining  = 0;
    size_t   varint_len = 0;
    switch (decode_varint(buf + 1, len - 1, remaining, varint_len)) {
    case VarintResult::NeedMore: return DecodeResult::NeedMore;
    case VarintResult::Malformed: return DecodeResult::Malformed;
    case VarintResult::Ok: break;
    }

    const size_t header_len = 1 + varint_len;
    if (len < header_len + remaining)
        return DecodeResult::NeedMore;

    out           = Packet();
    out.type      = static_cast<PacketType>(type_nibble);
    out.flags     = static_cast<uint8_t>(buf[0] & 0x0f);
    consumed      = header_len + remaining;

    const uint8_t* body     = buf + header_len;
    const size_t   body_len = remaining;

    switch (out.type) {
    case PacketType::Connack:
        if (body_len < 2)
            return DecodeResult::Malformed;
        out.return_code = body[1];
        break;

    case PacketType::Publish: {
        if (body_len < 2)
            return DecodeResult::Malformed;
        const size_t topic_len = (static_cast<size_t>(body[0]) << 8) | body[1];
        size_t       offset    = 2 + topic_len;
        if (body_len < offset)
            return DecodeResult::Malformed;
        out.topic.assign(reinterpret_cast<const char*>(body + 2), topic_len);

        const uint8_t qos = static_cast<uint8_t>((out.flags >> 1) & 0x03);
        if (qos > 0) {
            if (body_len < offset + 2)
                return DecodeResult::Malformed;
            out.packet_id = static_cast<uint16_t>((static_cast<uint16_t>(body[offset]) << 8) | body[offset + 1]);
            offset += 2;
        }
        out.payload.assign(reinterpret_cast<const char*>(body + offset), body_len - offset);
        break;
    }

    case PacketType::Puback:
    case PacketType::Pubrec:
    case PacketType::Pubrel:
    case PacketType::Pubcomp:
    case PacketType::Unsuback:
        if (body_len < 2)
            return DecodeResult::Malformed;
        out.packet_id = static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
        break;

    case PacketType::Suback:
        if (body_len < 3)
            return DecodeResult::Malformed;
        out.packet_id   = static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
        out.return_code = body[2];
        break;

    default:
        // PINGRESP / DISCONNECT and friends carry no variable header we use.
        break;
    }

    return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

struct MqttClient::Impl
{
    SSL_CTX* ctx = nullptr;
    SSL*     ssl = nullptr;
    bool     use_tls = true;
};

namespace {

std::string ssl_error_string()
{
    std::string out;
    unsigned long e;
    char          buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty())
            out += "; ";
        out += buf;
    }
    return out.empty() ? std::string("unknown TLS error") : out;
}

std::string socket_error_string()
{
#ifdef _WIN32
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return std::string(std::strerror(errno));
#endif
}

bool set_nonblocking(int fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool would_block()
{
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string random_client_id()
{
    static const char*                     hex = "0123456789abcdef";
    std::random_device                     rd;
    std::mt19937                           gen(rd());
    std::uniform_int_distribution<int>     dist(0, 15);
    std::string                            id = "orcaios";
    for (int i = 0; i < 12; ++i)
        id.push_back(hex[dist(gen)]);
    return id; // 19 chars: within the 23-char client-id limit older brokers enforce
}

#ifdef _WIN32
struct WinsockInit
{
    WinsockInit()
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
};
#endif

} // namespace

MqttClient::MqttClient() : m_impl(new Impl())
{
#ifdef _WIN32
    static WinsockInit s_winsock_init;
#endif
}

MqttClient::~MqttClient()
{
    disconnect();
    delete m_impl;
}

bool MqttClient::wait_readable(int timeout_ms)
{
    if (m_fd < 0)
        return false;
    struct pollfd pfd;
    pfd.fd      = m_fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    const int r = BL_POLL(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

bool MqttClient::wait_writable(int timeout_ms)
{
    if (m_fd < 0)
        return false;
    struct pollfd pfd;
    pfd.fd      = m_fd;
    pfd.events  = POLLOUT;
    pfd.revents = 0;
    const int r = BL_POLL(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & POLLOUT) != 0;
}

bool MqttClient::tcp_connect(const std::string& host, uint16_t port, int timeout_s, std::string& error)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    const int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0 || res == nullptr) {
        error = "cannot resolve " + host + ": " + gai_strerror(gai);
        return false;
    }

    bool ok = false;
    for (struct addrinfo* ai = res; ai != nullptr && !ok; ai = ai->ai_next) {
        const int fd = static_cast<int>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd == BL_INVALID_SOCKET)
            continue;
        if (!set_nonblocking(fd)) {
            BL_CLOSE(fd);
            continue;
        }
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

        const int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
        if (rc == 0) {
            m_fd = fd;
            ok   = true;
            break;
        }
        if (!would_block()
#ifndef _WIN32
            && errno != EINPROGRESS
#endif
        ) {
            error = "connect failed: " + socket_error_string();
            BL_CLOSE(fd);
            continue;
        }

        m_fd = fd;
        if (!wait_writable(timeout_s * 1000)) {
            error = "connection to " + host + " timed out";
            close_socket();
            continue;
        }
        int       so_error = 0;
        socklen_t len      = sizeof(so_error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len) != 0 || so_error != 0) {
            error = "connect failed: " + std::string(std::strerror(so_error));
            close_socket();
            continue;
        }
        ok = true;
    }
    freeaddrinfo(res);

    if (!ok && error.empty())
        error = "cannot reach " + host + ":" + port_str;
    return ok;
}

bool MqttClient::tls_handshake(const std::string& host, int timeout_s, std::string& error)
{
    (void) host;
    m_impl->ctx = SSL_CTX_new(TLS_client_method());
    if (m_impl->ctx == nullptr) {
        error = "SSL_CTX_new: " + ssl_error_string();
        return false;
    }
    // The printer serves a self-signed certificate that no store can chain to,
    // and in LAN mode there is no name to verify against either - authentication
    // is the access code carried inside the MQTT CONNECT. Verification is off by
    // necessity; the security level is lowered along with it because the
    // firmware's certificate and cipher suites predate OpenSSL 3's defaults.
    SSL_CTX_set_verify(m_impl->ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_security_level(m_impl->ctx, 0);
    SSL_CTX_set_min_proto_version(m_impl->ctx, TLS1_VERSION);
    SSL_CTX_set_cipher_list(m_impl->ctx, "ALL:@SECLEVEL=0");

    m_impl->ssl = SSL_new(m_impl->ctx);
    if (m_impl->ssl == nullptr) {
        error = "SSL_new: " + ssl_error_string();
        return false;
    }
    SSL_set_fd(m_impl->ssl, m_fd);

    const int64_t deadline = now_ms() + static_cast<int64_t>(timeout_s) * 1000;
    for (;;) {
        ERR_clear_error();
        const int rc = SSL_connect(m_impl->ssl);
        if (rc == 1)
            return true;

        const int err = SSL_get_error(m_impl->ssl, rc);
        const int remaining = static_cast<int>(deadline - now_ms());
        if (remaining <= 0) {
            error = "TLS handshake timed out";
            return false;
        }
        if (err == SSL_ERROR_WANT_READ) {
            if (!wait_readable(remaining)) {
                error = "TLS handshake timed out";
                return false;
            }
        } else if (err == SSL_ERROR_WANT_WRITE) {
            if (!wait_writable(remaining)) {
                error = "TLS handshake timed out";
                return false;
            }
        } else {
            error = "TLS handshake failed: " + ssl_error_string();
            return false;
        }
    }
}

bool MqttClient::write_all(const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> lock(m_write_mutex);
    size_t sent = 0;
    while (sent < len) {
        if (m_fd < 0)
            return false;
        int rc;
        if (m_impl->use_tls) {
            ERR_clear_error();
            rc = SSL_write(m_impl->ssl, data + sent, static_cast<int>(len - sent));
            if (rc <= 0) {
                const int err = SSL_get_error(m_impl->ssl, rc);
                if (err == SSL_ERROR_WANT_WRITE) {
                    if (!wait_writable(5000))
                        return false;
                    continue;
                }
                if (err == SSL_ERROR_WANT_READ) {
                    if (!wait_readable(5000))
                        return false;
                    continue;
                }
                return false;
            }
        } else {
            rc = static_cast<int>(::send(m_fd, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0));
            if (rc <= 0) {
                if (rc < 0 && would_block()) {
                    if (!wait_writable(5000))
                        return false;
                    continue;
                }
                return false;
            }
        }
        sent += static_cast<size_t>(rc);
    }
    return true;
}

int MqttClient::read_some(uint8_t* data, size_t len, int timeout_ms)
{
    if (m_fd < 0)
        return -1;

    // SSL_read must not run concurrently with SSL_write on the same SSL object.
    // The receive thread only takes the lock once poll() (or SSL_pending)
    // already says there is something to consume, so a publisher never waits on
    // a blocked reader.
    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (m_impl->use_tls) {
        ERR_clear_error();
        const int rc = SSL_read(m_impl->ssl, data, static_cast<int>(len));
        if (rc > 0)
            return rc;
        const int err = SSL_get_error(m_impl->ssl, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            return 0;
        if (err == SSL_ERROR_ZERO_RETURN)
            return -1;
        return -1;
    }

    (void) timeout_ms;
    const int rc = static_cast<int>(::recv(m_fd, reinterpret_cast<char*>(data), static_cast<int>(len), 0));
    if (rc > 0)
        return rc;
    if (rc < 0 && would_block())
        return 0;
    return -1;
}

void MqttClient::close_socket()
{
    if (m_impl->ssl != nullptr) {
        SSL_free(m_impl->ssl);
        m_impl->ssl = nullptr;
    }
    if (m_impl->ctx != nullptr) {
        SSL_CTX_free(m_impl->ctx);
        m_impl->ctx = nullptr;
    }
    if (m_fd >= 0) {
        BL_CLOSE(m_fd);
        m_fd = -1;
    }
}

void MqttClient::teardown_locked(bool send_disconnect)
{
    const bool was_connected = m_connected.exchange(false);
    m_stop.store(true);
    // A deliberate teardown is not a lost connection. Claiming the one-shot
    // flag here also means the receive thread cannot be inside the callback
    // while the owner is destroying itself right after this call.
    m_lost_fired.store(true);

    if (send_disconnect && was_connected && m_fd >= 0) {
        const std::vector<uint8_t> pkt = encode_disconnect();
        write_all(pkt.data(), pkt.size());
    }

    // Shut the socket down before joining: the receive thread is parked in
    // poll(), and a half-close wakes it immediately instead of after its
    // poll slice.
    if (m_fd >= 0) {
#ifdef _WIN32
        ::shutdown(m_fd, SD_BOTH);
#else
        ::shutdown(m_fd, SHUT_RDWR);
#endif
    }
    if (m_rx_thread.joinable()) {
        if (m_rx_thread.get_id() == std::this_thread::get_id())
            m_rx_thread.detach(); // called from inside the lost callback
        else
            m_rx_thread.join();
    }
    close_socket();
}

int MqttClient::connect(const MqttConfig& cfg, std::string& error)
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycle_mutex);
    error.clear();

    // A previous session may have ended on its own (connection lost), leaving a
    // finished-but-joinable receive thread behind. Assigning over a joinable
    // std::thread calls std::terminate, so always tear the old one down first.
    teardown_locked(false);

    m_stop.store(false);
    m_lost_fired.store(false);
    m_impl->use_tls = cfg.use_tls;
    m_keepalive_s   = cfg.keepalive_s == 0 ? 30 : cfg.keepalive_s;

    if (!tcp_connect(cfg.host, cfg.port, cfg.connect_timeout_s, error)) {
        close_socket();
        return ConnackTransportError;
    }
    if (cfg.use_tls && !tls_handshake(cfg.host, cfg.connect_timeout_s, error)) {
        close_socket();
        return ConnackTransportError;
    }

    const std::string client_id = cfg.client_id.empty() ? random_client_id() : cfg.client_id;
    const std::vector<uint8_t> connect_pkt = encode_connect(client_id, cfg.username, cfg.password, m_keepalive_s);
    if (!write_all(connect_pkt.data(), connect_pkt.size())) {
        error = "failed to send MQTT CONNECT";
        close_socket();
        return ConnackTransportError;
    }

    // Wait for CONNACK.
    std::vector<uint8_t> buffer;
    const int64_t        deadline = now_ms() + static_cast<int64_t>(cfg.connect_timeout_s) * 1000;
    for (;;) {
        Packet packet;
        size_t consumed = 0;
        const DecodeResult dr = buffer.empty() ? DecodeResult::NeedMore
                                               : decode_packet(buffer.data(), buffer.size(), packet, consumed);
        if (dr == DecodeResult::Malformed) {
            error = "malformed reply from printer (not an MQTT broker?)";
            close_socket();
            return ConnackTransportError;
        }
        if (dr == DecodeResult::Ok) {
            if (packet.type != PacketType::Connack) {
                error = "unexpected MQTT packet before CONNACK";
                close_socket();
                return ConnackTransportError;
            }
            if (packet.return_code != ConnackAccepted) {
                error = "printer refused the connection (CONNACK " + std::to_string(packet.return_code) + ")";
                close_socket();
                return static_cast<int>(packet.return_code);
            }
            break;
        }

        const int remaining = static_cast<int>(deadline - now_ms());
        if (remaining <= 0) {
            error = "no CONNACK from printer";
            close_socket();
            return ConnackTransportError;
        }
        if (!wait_readable(remaining))
            continue;

        uint8_t   chunk[2048];
        const int n = read_some(chunk, sizeof(chunk), remaining);
        if (n < 0) {
            error = "connection closed during MQTT handshake";
            close_socket();
            return ConnackTransportError;
        }
        if (n > 0)
            buffer.insert(buffer.end(), chunk, chunk + n);
    }

    m_connected.store(true);
    m_rx_thread = std::thread(&MqttClient::rx_loop, this);
    return ConnackAccepted;
}

void MqttClient::disconnect()
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycle_mutex);
    teardown_locked(true);
}

void MqttClient::fire_lost(const std::string& reason)
{
    if (m_lost_fired.exchange(true))
        return;
    m_connected.store(false);
    if (m_lost_fn)
        m_lost_fn(reason);
}

bool MqttClient::subscribe(const std::string& topic, uint8_t qos)
{
    if (!m_connected.load())
        return false;
    const uint16_t id  = m_next_packet_id.fetch_add(1);
    const std::vector<uint8_t> pkt = encode_subscribe(id == 0 ? 1 : id, topic, qos);
    return write_all(pkt.data(), pkt.size());
}

bool MqttClient::publish(const std::string& topic, const std::string& payload, uint8_t qos)
{
    if (!m_connected.load())
        return false;
    const uint16_t id  = m_next_packet_id.fetch_add(1);
    const std::vector<uint8_t> pkt = encode_publish(topic, payload, qos, id == 0 ? 1 : id);
    return write_all(pkt.data(), pkt.size());
}

void MqttClient::rx_loop()
{
    std::vector<uint8_t> buffer;
    int64_t              last_write_ms = now_ms();
    int64_t              last_read_ms  = now_ms();
    const int64_t        ping_interval_ms = static_cast<int64_t>(m_keepalive_s) * 1000 / 2;
    // Two missed keepalive periods with nothing at all from the printer means
    // the link is gone even though the socket has not reported an error - Wi-Fi
    // drops and printer reboots both look like this.
    const int64_t        dead_after_ms = static_cast<int64_t>(m_keepalive_s) * 2000;

    while (!m_stop.load()) {
        bool have_data = false;
        {
            std::lock_guard<std::mutex> lock(m_write_mutex);
            have_data = m_impl->use_tls && m_impl->ssl != nullptr && SSL_pending(m_impl->ssl) > 0;
        }
        if (!have_data)
            have_data = wait_readable(200);

        if (m_stop.load())
            break;

        if (have_data) {
            uint8_t   chunk[8192];
            const int n = read_some(chunk, sizeof(chunk), 200);
            if (n < 0) {
                if (!m_stop.load())
                    fire_lost("connection to printer closed");
                return;
            }
            if (n > 0) {
                last_read_ms = now_ms();
                buffer.insert(buffer.end(), chunk, chunk + n);
            }
        }

        // Drain every complete packet in the buffer.
        for (;;) {
            Packet packet;
            size_t consumed = 0;
            const DecodeResult dr = buffer.empty() ? DecodeResult::NeedMore
                                                   : decode_packet(buffer.data(), buffer.size(), packet, consumed);
            if (dr == DecodeResult::NeedMore)
                break;
            if (dr == DecodeResult::Malformed) {
                fire_lost("malformed MQTT stream from printer");
                return;
            }
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(consumed));

            switch (packet.type) {
            case PacketType::Publish: {
                const uint8_t qos = static_cast<uint8_t>((packet.flags >> 1) & 0x03);
                if (qos == 1) {
                    const std::vector<uint8_t> ack = encode_puback(packet.packet_id);
                    write_all(ack.data(), ack.size());
                    last_write_ms = now_ms();
                }
                if (m_message_fn)
                    m_message_fn(packet.topic, packet.payload);
                break;
            }
            case PacketType::Disconnect:
                fire_lost("printer sent DISCONNECT");
                return;
            default:
                // CONNACK/SUBACK/PUBACK/PINGRESP: arrival is the only signal we
                // need, and it already refreshed last_read_ms.
                break;
            }
        }

        const int64_t now = now_ms();
        if (now - last_write_ms >= ping_interval_ms) {
            const std::vector<uint8_t> ping = encode_pingreq();
            if (!write_all(ping.data(), ping.size())) {
                fire_lost("keepalive failed");
                return;
            }
            last_write_ms = now;
        }
        if (now - last_read_ms >= dead_after_ms) {
            fire_lost("printer stopped responding");
            return;
        }
    }
}

} // namespace BambuLan
} // namespace Slic3r
