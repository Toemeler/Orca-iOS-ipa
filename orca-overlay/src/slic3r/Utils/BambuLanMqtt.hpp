// Bambu LAN backend - MQTT 3.1.1 client over TLS
// ----------------------------------------------
// Orca reaches Bambu printers through a closed-source plugin that does not
// exist for iOS (and cannot be side-loaded there: iOS refuses to dlopen a
// downloaded dylib). LAN mode itself is plain, documented networking - MQTT
// over TLS on port 8883 for status and control, FTPS on 990 for the payload -
// so this file implements the MQTT half directly.
//
// Deliberately free of every Orca/wx/boost dependency: it is plain C++17 plus
// OpenSSL, which keeps it compilable for both iOS slices and lets the codec run
// under the host-side self test in tools/bambu-lan/.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef slic3r_BambuLanMqtt_hpp_
#define slic3r_BambuLanMqtt_hpp_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Slic3r {
namespace BambuLan {

// ---------------------------------------------------------------------------
// Wire codec (pure, no I/O - covered by the host self test)
// ---------------------------------------------------------------------------

enum class PacketType : uint8_t {
    Invalid     = 0,
    Connect     = 1,
    Connack     = 2,
    Publish     = 3,
    Puback      = 4,
    Pubrec      = 5,
    Pubrel      = 6,
    Pubcomp     = 7,
    Subscribe   = 8,
    Suback      = 9,
    Unsubscribe = 10,
    Unsuback    = 11,
    Pingreq     = 12,
    Pingresp    = 13,
    Disconnect  = 14,
};

struct Packet
{
    PacketType  type = PacketType::Invalid;
    uint8_t     flags = 0;      // low nibble of the fixed header
    std::string topic;          // PUBLISH only
    std::string payload;        // PUBLISH only
    uint16_t    packet_id = 0;  // PUBLISH (qos>0), PUBACK, SUBACK
    uint8_t     return_code = 0;// CONNACK / SUBACK
};

// Remaining-length varint. Both directions, because a truncated read has to be
// told apart from a malformed one.
void encode_varint(std::vector<uint8_t>& out, uint32_t value);

enum class VarintResult { Ok, NeedMore, Malformed };
VarintResult decode_varint(const uint8_t* data, size_t len, uint32_t& value, size_t& bytes_used);

// MQTT 3.1.1 CONNECT. clean_session is always set: the printer's state comes
// from a full push report after connecting, so a resumed session buys nothing.
std::vector<uint8_t> encode_connect(const std::string& client_id,
                                    const std::string& username,
                                    const std::string& password,
                                    uint16_t           keepalive_s);

std::vector<uint8_t> encode_subscribe(uint16_t packet_id, const std::string& topic, uint8_t qos);
std::vector<uint8_t> encode_publish(const std::string& topic, const std::string& payload, uint8_t qos, uint16_t packet_id);
std::vector<uint8_t> encode_puback(uint16_t packet_id);
std::vector<uint8_t> encode_pingreq();
std::vector<uint8_t> encode_disconnect();

// Tries to peel one packet off the front of `buf`.
//   Ok        -> `consumed` bytes belong to `out`
//   NeedMore  -> incomplete, keep reading
//   Malformed -> the stream is unusable, drop the connection
enum class DecodeResult { Ok, NeedMore, Malformed };
DecodeResult decode_packet(const uint8_t* buf, size_t len, Packet& out, size_t& consumed);

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

struct MqttConfig
{
    std::string host;
    uint16_t    port = 8883;
    std::string username = "bblp";
    std::string password;          // LAN access code from the printer's screen
    std::string client_id;         // empty -> generated
    uint16_t    keepalive_s = 30;
    int         connect_timeout_s = 10;
    bool        use_tls = true;    // false is only used by the host self test
};

// CONNACK return codes, as sent to Orca's OnLocalConnectedFn message argument.
// Orca's GUI_App special-cases "5" (not authorised) to mean "wrong access code".
enum ConnackCode : int {
    ConnackAccepted            = 0,
    ConnackUnacceptableVersion = 1,
    ConnackIdentifierRejected  = 2,
    ConnackServerUnavailable   = 3,
    ConnackBadCredentials      = 4,
    ConnackNotAuthorized       = 5,
    // Locally generated, never on the wire.
    ConnackTransportError      = 100,
};

class MqttClient
{
public:
    // topic, payload
    using MessageFn = std::function<void(const std::string&, const std::string&)>;
    // reason string for logging; fired once, from the receive thread
    using LostFn = std::function<void(const std::string&)>;

    MqttClient();
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    void set_message_fn(MessageFn fn) { m_message_fn = std::move(fn); }
    void set_lost_fn(LostFn fn) { m_lost_fn = std::move(fn); }

    // Blocking: TCP connect, TLS handshake, CONNECT/CONNACK. Returns a
    // ConnackCode; `error` carries a human-readable reason on failure.
    int connect(const MqttConfig& cfg, std::string& error);

    // Graceful DISCONNECT + socket teardown. Safe to call when not connected,
    // and safe to call from a different thread than the receive thread.
    void disconnect();

    bool is_connected() const { return m_connected.load(); }

    bool subscribe(const std::string& topic, uint8_t qos = 0);
    bool publish(const std::string& topic, const std::string& payload, uint8_t qos = 0);

private:
    bool tcp_connect(const std::string& host, uint16_t port, int timeout_s, std::string& error);
    bool tls_handshake(const std::string& host, int timeout_s, std::string& error);
    bool write_all(const uint8_t* data, size_t len);
    // -1 error/closed, 0 timeout, >0 bytes
    int  read_some(uint8_t* data, size_t len, int timeout_ms);
    bool wait_readable(int timeout_ms);
    bool wait_writable(int timeout_ms);
    void close_socket();
    // Stops the receive thread and closes the socket. The caller must hold
    // m_lifecycle_mutex.
    void teardown_locked(bool send_disconnect);
    void rx_loop();
    void fire_lost(const std::string& reason);

    struct Impl;                 // hides the OpenSSL types from Orca's TUs
    Impl*             m_impl = nullptr;
    int               m_fd = -1;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_lost_fired{false};
    std::thread       m_rx_thread;
    std::mutex        m_write_mutex;
    std::mutex        m_lifecycle_mutex;
    std::atomic<uint16_t> m_next_packet_id{1};
    uint16_t          m_keepalive_s = 30;
    MessageFn         m_message_fn;
    LostFn            m_lost_fn;
};

} // namespace BambuLan
} // namespace Slic3r

#endif // slic3r_BambuLanMqtt_hpp_
