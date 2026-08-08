// Bambu LAN backend - built-in camera over TLS. See BambuLanCamera.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanCamera.hpp"

#include <chrono>
#include <cstring>

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

#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace BambuLan {

namespace {

// The auth frame. 80 bytes, fixed layout, sent once and never repeated:
//
//   uint32  0x00000040   payload length that follows the four header words
//   uint32  0x00003000   message type
//   uint32  0
//   uint32  0
//   char[32] username, NUL padded
//   char[32] access code, NUL padded
//
// All little-endian. The printer does not answer it - a wrong code shows up as
// the connection closing rather than as an error message.
constexpr size_t AUTH_PACKET_SIZE = 80;
constexpr size_t FRAME_HEADER_SIZE = 16;

// A frame larger than this is a desynchronised stream rather than a picture;
// the printer's own are a few hundred kilobytes at 1080p.
constexpr uint32_t MAX_FRAME_SIZE = 8u * 1024u * 1024u;

void put_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint32_t get_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void put_padded(std::vector<uint8_t>& out, const std::string& s, size_t width)
{
    const size_t n = s.size() < width ? s.size() : width;
    out.insert(out.end(), s.begin(), s.begin() + n);
    out.insert(out.end(), width - n, 0);
}

std::string ssl_error_string()
{
    const unsigned long e = ERR_get_error();
    if (e == 0)
        return "no OpenSSL error";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    return buf;
}

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

struct CameraClient::Impl
{
#ifdef _WIN32
    SOCKET fd = BL_INVALID_SOCKET;
#else
    int fd = BL_INVALID_SOCKET;
#endif
    SSL_CTX* ctx = nullptr;
    SSL*     ssl = nullptr;

    void close_all()
    {
        if (ssl != nullptr) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl = nullptr;
        }
        if (ctx != nullptr) {
            SSL_CTX_free(ctx);
            ctx = nullptr;
        }
        if (fd != BL_INVALID_SOCKET) {
            BL_CLOSE(fd);
            fd = BL_INVALID_SOCKET;
        }
    }

    bool wait_readable(int timeout_ms) const
    {
        if (fd == BL_INVALID_SOCKET)
            return false;
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;
        return BL_POLL(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN) != 0;
    }

    bool wait_writable(int timeout_ms) const
    {
        if (fd == BL_INVALID_SOCKET)
            return false;
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLOUT;
        pfd.revents = 0;
        return BL_POLL(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLOUT) != 0;
    }
};

CameraClient::CameraClient() : m_impl(new Impl()) {}

CameraClient::~CameraClient() { stop(); }

bool CameraClient::start(const Config& cfg)
{
    if (m_running.load())
        return false;
    m_stop.store(false);
    m_frames.store(0);
    m_running.store(true);
    m_thread = std::thread([this, cfg] { this->run(cfg); });
    return true;
}

void CameraClient::stop()
{
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
    m_running.store(false);
}

void CameraClient::run(Config cfg)
{
    std::string error;

    auto fail = [&](const std::string& why) {
        m_impl->close_all();
        m_running.store(false);
        // Not while stopping: a caller that asked for the stream to end does
        // not want to be told the socket closed.
        if (!m_stop.load() && m_error_fn)
            m_error_fn(why);
        BOOST_LOG_TRIVIAL(error) << "BambuLanCamera: " << why;
    };

    // ---- connect --------------------------------------------------------
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_str = std::to_string(cfg.port);
    struct addrinfo*  res      = nullptr;
    if (::getaddrinfo(cfg.host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        fail("cannot resolve " + cfg.host);
        return;
    }

    bool connected = false;
    for (struct addrinfo* ai = res; ai != nullptr && !connected; ai = ai->ai_next) {
        const auto fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == BL_INVALID_SOCKET)
            continue;
#ifndef _WIN32
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

        m_impl->fd = fd;
        if (::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
            connected = true;
            break;
        }
#ifndef _WIN32
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            BL_CLOSE(fd);
            m_impl->fd = BL_INVALID_SOCKET;
            continue;
        }
#endif
        if (!m_impl->wait_writable(cfg.connect_timeout_s * 1000)) {
            BL_CLOSE(fd);
            m_impl->fd = BL_INVALID_SOCKET;
            continue;
        }
        int       so_error = 0;
        socklen_t len      = sizeof(so_error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len) != 0 ||
            so_error != 0) {
            BL_CLOSE(fd);
            m_impl->fd = BL_INVALID_SOCKET;
            continue;
        }
        connected = true;
    }
    ::freeaddrinfo(res);

    if (!connected) {
        fail("cannot reach " + cfg.host + ":" + port_str);
        return;
    }

    // ---- TLS ------------------------------------------------------------
    //
    // Same terms as the MQTT and FTPS links: the printer's certificate is
    // self-signed and there is no name to verify it against, so verification is
    // off and the security level is lowered to let OpenSSL 3 talk to firmware
    // that predates its defaults. The access code in the auth frame is what
    // authenticates the session.
    m_impl->ctx = SSL_CTX_new(TLS_client_method());
    if (m_impl->ctx == nullptr) {
        fail("SSL_CTX_new: " + ssl_error_string());
        return;
    }
    SSL_CTX_set_verify(m_impl->ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_security_level(m_impl->ctx, 0);
    SSL_CTX_set_min_proto_version(m_impl->ctx, TLS1_VERSION);
    SSL_CTX_set_cipher_list(m_impl->ctx, "ALL:@SECLEVEL=0");

    m_impl->ssl = SSL_new(m_impl->ctx);
    if (m_impl->ssl == nullptr) {
        fail("SSL_new: " + ssl_error_string());
        return;
    }
    SSL_set_fd(m_impl->ssl, static_cast<int>(m_impl->fd));

    const int64_t deadline = now_ms() + static_cast<int64_t>(cfg.connect_timeout_s) * 1000;
    for (;;) {
        if (m_stop.load()) {
            // Worth a line: a stop during the handshake used to leave no trace
            // at all, so "streaming from ..." followed by "stopped after 0
            // frames" looked like the printer refusing us rather than the app
            // pulling the plug.
            BOOST_LOG_TRIVIAL(error) << "BambuLanCamera: stopped during the TLS handshake";
            m_impl->close_all();
            m_running.store(false);
            return;
        }
        ERR_clear_error();
        const int rc = SSL_connect(m_impl->ssl);
        if (rc == 1)
            break;
        const int err       = SSL_get_error(m_impl->ssl, rc);
        const int remaining = static_cast<int>(deadline - now_ms());
        if (remaining <= 0) {
            fail("TLS handshake to the camera timed out");
            return;
        }
        if (err == SSL_ERROR_WANT_READ) {
            m_impl->wait_readable(remaining < 200 ? remaining : 200);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            m_impl->wait_writable(remaining < 200 ? remaining : 200);
        } else {
            fail("TLS handshake to the camera failed: " + ssl_error_string());
            return;
        }
    }

    // ---- authenticate ---------------------------------------------------
    std::vector<uint8_t> auth;
    auth.reserve(AUTH_PACKET_SIZE);
    put_u32_le(auth, 0x40);
    put_u32_le(auth, 0x3000);
    put_u32_le(auth, 0);
    put_u32_le(auth, 0);
    put_padded(auth, cfg.username, 32);
    put_padded(auth, cfg.access_code, 32);

    size_t sent = 0;
    while (sent < auth.size()) {
        if (m_stop.load()) {
            BOOST_LOG_TRIVIAL(error) << "BambuLanCamera: stopped while sending the auth frame";
            m_impl->close_all();
            m_running.store(false);
            return;
        }
        ERR_clear_error();
        const int n = SSL_write(m_impl->ssl, auth.data() + sent, static_cast<int>(auth.size() - sent));
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        const int err = SSL_get_error(m_impl->ssl, n);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            m_impl->wait_writable(200);
            continue;
        }
        fail("sending the camera auth frame failed: " + ssl_error_string());
        return;
    }

    BOOST_LOG_TRIVIAL(error) << "BambuLanCamera: authenticated to " << cfg.host << ":" << cfg.port
                             << ", waiting for frames";

    // ---- stream ---------------------------------------------------------
    //
    // Each frame is a 16-byte header - the first four bytes are the JPEG's
    // length, the rest is a timestamp we have no use for - followed by that
    // many bytes of image. Read into one buffer and hand out whole frames as
    // they complete, because TLS records do not align with them.
    std::vector<uint8_t> buffer;
    buffer.reserve(512 * 1024);
    uint8_t chunk[32 * 1024];

    while (!m_stop.load()) {
        const bool pending = SSL_pending(m_impl->ssl) > 0;
        if (!pending && !m_impl->wait_readable(200))
            continue;

        ERR_clear_error();
        const int n = SSL_read(m_impl->ssl, chunk, static_cast<int>(sizeof(chunk)));
        if (n <= 0) {
            const int err = SSL_get_error(m_impl->ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                continue;
            // A wrong access code looks exactly like this: the handshake and
            // the write both succeed, and then the printer hangs up without a
            // byte of payload. Say so when it happens before the first frame.
            if (m_frames.load() == 0)
                fail("the camera closed the connection before sending anything - "
                     "check the access code, and that the camera is enabled on the printer");
            else
                fail("the camera connection closed");
            return;
        }

        buffer.insert(buffer.end(), chunk, chunk + n);

        for (;;) {
            if (buffer.size() < FRAME_HEADER_SIZE)
                break;
            const uint32_t payload = get_u32_le(buffer.data());
            if (payload == 0 || payload > MAX_FRAME_SIZE) {
                fail("the camera stream desynchronised (frame claims " +
                     std::to_string(payload) + " bytes)");
                return;
            }
            if (buffer.size() < FRAME_HEADER_SIZE + payload)
                break;

            if (m_frame_fn)
                m_frame_fn(buffer.data() + FRAME_HEADER_SIZE, payload);
            m_frames.fetch_add(1);

            buffer.erase(buffer.begin(),
                         buffer.begin() + static_cast<long>(FRAME_HEADER_SIZE + payload));
        }
    }

    m_impl->close_all();
    m_running.store(false);
}

} // namespace BambuLan
} // namespace Slic3r
