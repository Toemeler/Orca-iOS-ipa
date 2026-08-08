///|/ Copyright (c) 2026 Orca-iOS-ipa contributors
///|/
///|/ Orca-iOS-ipa is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BambuLanCamera_hpp_
#define slic3r_BambuLanCamera_hpp_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Slic3r {
namespace BambuLan {

// The printer's built-in camera, over the LAN and without the cloud.
//
// Every Bambu printer that has a camera serves it on TCP 6000 behind TLS: send
// one fixed 80-byte frame carrying the username and the access code, and it
// answers with an unbounded sequence of complete JPEG images, each preceded by
// a 16-byte header whose first four bytes are the payload length. There is no
// container, no negotiation and no keep-alive - the connection simply stays
// open and frames arrive at whatever rate the printer manages.
//
// That is the whole protocol, and it is the reason this is worth doing here
// rather than through wxMediaCtrl: there is no stream for a media player to
// open. Orca's own path builds an rtsps:// URL for the Bambu plugin to consume,
// and both of those are absent on iOS.
class CameraClient
{
public:
    struct Config
    {
        std::string host;                 // printer IP
        std::string username = "bblp";    // always bblp for LAN mode
        std::string access_code;
        uint16_t    port            = 6000;
        int         connect_timeout_s = 5;
    };

    // Called from the streaming thread, once per decoded frame, with the raw
    // JPEG bytes. The callee must not block: the next frame is already on its
    // way and nothing here buffers.
    using OnFrameFn  = std::function<void(const uint8_t* data, size_t size)>;
    // Called from the streaming thread when the connection ends, with the
    // reason. Never called after stop() returns.
    using OnErrorFn  = std::function<void(const std::string& reason)>;

    CameraClient();
    ~CameraClient();

    CameraClient(const CameraClient&)            = delete;
    CameraClient& operator=(const CameraClient&) = delete;

    void set_frame_fn(OnFrameFn fn) { m_frame_fn = std::move(fn); }
    void set_error_fn(OnErrorFn fn) { m_error_fn = std::move(fn); }

    // Connects and starts streaming on a thread of its own. Returns false only
    // if a stream is already running; connection failures arrive through the
    // error callback, because they can happen long after this returns.
    bool start(const Config& cfg);

    // Joins the streaming thread. Safe to call when not running, and safe to
    // call twice.
    void stop();

    bool is_running() const { return m_running.load(); }

    // Frames seen since start(), for diagnosing "the view is blank": zero says
    // the connection never produced anything, non-zero says the decode or the
    // drawing is at fault.
    uint64_t frame_count() const { return m_frames.load(); }

private:
    void run(Config cfg);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    OnFrameFn             m_frame_fn;
    OnErrorFn             m_error_fn;
    std::thread           m_thread;
    std::atomic<bool>     m_stop{false};
    std::atomic<bool>     m_running{false};
    std::atomic<uint64_t> m_frames{0};
};

} // namespace BambuLan
} // namespace Slic3r

#endif // slic3r_BambuLanCamera_hpp_
