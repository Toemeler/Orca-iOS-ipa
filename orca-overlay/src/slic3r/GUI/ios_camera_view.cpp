///|/ Copyright (c) 2026 Orca-iOS-ipa contributors
///|/
///|/ Orca-iOS-ipa is released under the terms of the AGPLv3 or higher
///|/
// The printer's camera on iOS: wxMediaCtrl2, backed by BambuLan::CameraClient.
//
// On every other platform wxMediaCtrl2 wraps a media player and is handed an
// rtsps:// URL that Bambu's closed plugin serves from a companion process. Both
// halves of that are missing here - there is no plugin and no companion - so
// this implements the same small interface directly against the printer:
// connect, authenticate, receive JPEGs, draw them.
//
// It deliberately keeps wxMediaCtrl2's shape rather than introducing a new
// widget, so StatusPanel and MediaPlayCtrl keep their play button, their status
// label and their state machine, and nothing above this file needs to know the
// difference.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/uri.h>
#include <wx/window.h>

#include <boost/log/trivial.hpp>

#include "wxMediaCtrl2.h"
#include "../Utils/BambuLanCamera.hpp"

namespace {

// Everything the paint handler and the streaming thread share.
struct CameraState
{
    std::mutex           mutex;
    wxImage              frame;        // latest decoded frame, guarded by mutex
    std::atomic<bool>    have_frame{false};
    std::atomic<bool>    failed{false};
    std::string          error;        // guarded by mutex
    Slic3r::BambuLan::CameraClient client;
    std::string          host;
    std::string          access_code;
};

CameraState *state_of(void *&slot)
{
    if (slot == nullptr)
        slot = new CameraState();
    return static_cast<CameraState *>(slot);
}

} // namespace

wxDEFINE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);

// A private event so the streaming thread can wake the GUI without touching it.
wxDEFINE_EVENT(EVT_ORCA_IOS_CAMERA_FRAME, wxCommandEvent);

wxMediaCtrl2::wxMediaCtrl2(wxWindow *parent)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxBLACK);

    m_player = nullptr; // reused as the CameraState pointer

    Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();

        CameraState *const st = static_cast<CameraState *>(m_player);
        if (st == nullptr)
            return;

        if (st->have_frame.load()) {
            wxImage img;
            {
                std::lock_guard<std::mutex> lock(st->mutex);
                img = st->frame;
            }
            if (img.IsOk()) {
                // Fit, do not fill: a stretched camera picture is worse than
                // black bars, and the printer's aspect ratio is not the panel's.
                const wxSize cs = GetClientSize();
                if (cs.x > 0 && cs.y > 0 && img.GetWidth() > 0 && img.GetHeight() > 0) {
                    const double sx = double(cs.x) / img.GetWidth();
                    const double sy = double(cs.y) / img.GetHeight();
                    const double s  = sx < sy ? sx : sy;
                    const int    w  = int(img.GetWidth() * s);
                    const int    h  = int(img.GetHeight() * s);
                    if (w > 0 && h > 0) {
                        wxBitmap bmp(img.Scale(w, h, wxIMAGE_QUALITY_NORMAL));
                        dc.DrawBitmap(bmp, (cs.x - w) / 2, (cs.y - h) / 2, false);
                    }
                }
            }
            return;
        }

        // Nothing to draw yet. Say why rather than showing an unexplained black
        // rectangle - "connecting" and "wrong access code" look identical
        // otherwise, and the second one never resolves.
        wxString msg;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            msg = st->failed.load() ? wxString::FromUTF8(st->error)
                                    : (st->client.is_running() ? wxString("Connecting to the camera...")
                                                               : wxString());
        }
        if (!msg.IsEmpty()) {
            dc.SetTextForeground(*wxWHITE);
            const wxSize ts = dc.GetTextExtent(msg);
            const wxSize cs = GetClientSize();
            dc.DrawText(msg, (cs.x - ts.x) / 2, (cs.y - ts.y) / 2);
        }
    });

    Bind(EVT_ORCA_IOS_CAMERA_FRAME, [this](wxCommandEvent &) { Refresh(false); });
}

wxMediaCtrl2::~wxMediaCtrl2()
{
    CameraState *const st = static_cast<CameraState *>(m_player);
    if (st != nullptr) {
        st->client.stop();
        delete st;
        m_player = nullptr;
    }
}

void wxMediaCtrl2::Load(wxURI url)
{
    CameraState *const st = state_of(m_player);
    st->client.stop();
    st->have_frame.store(false);
    st->failed.store(false);

    // MediaPlayCtrl already builds exactly the URL this needs, for the same
    // camera, on every platform:
    //
    //   bambu:///local/<ip>.?port=6000&user=bblp&passwd=<access code>
    //
    // so parse that rather than inventing a scheme. The trailing dot after the
    // address is Orca's, not a typo. The rtsps variant of the same URL carries
    // the credentials in its authority instead; it is only chosen when the
    // printer advertises RTSP, which the LAN backend here never asks for.
    st->host.clear();
    st->access_code.clear();

    const wxString path  = url.HasPath() ? url.GetPath() : wxString();
    const wxString query = url.HasQuery() ? url.GetQuery() : wxString();

    const int local = path.Find("/local/");
    if (local != wxNOT_FOUND) {
        wxString host = path.Mid(local + 7);
        if (host.EndsWith("."))
            host.RemoveLast();
        st->host = host.utf8_string();
    }

    const int pw = query.Find("passwd=");
    if (pw != wxNOT_FOUND) {
        wxString code = query.Mid(pw + 7);
        const int amp = code.Find('&');
        if (amp != wxNOT_FOUND)
            code = code.Left(amp);
        st->access_code = code.utf8_string();
    }

    BOOST_LOG_TRIVIAL(error) << "orca-ios-camera: load host=" << st->host
                             << " code=" << (st->access_code.empty() ? "MISSING" : "set");
    Refresh(false);
}

void wxMediaCtrl2::Play()
{
    CameraState *const st = state_of(m_player);
    if (st->client.is_running())
        return;
    if (st->host.empty() || st->access_code.empty()) {
        std::lock_guard<std::mutex> lock(st->mutex);
        st->error = "No camera address. Set the printer's IP and access code.";
        st->failed.store(true);
        Refresh(false);
        return;
    }

    st->failed.store(false);

    // Both callbacks run on the streaming thread, so neither touches a wx
    // object: the frame is decoded and stored under the mutex, and the GUI is
    // woken with a queued event.
    st->client.set_frame_fn([this, st](const uint8_t *data, size_t size) {
        wxMemoryInputStream in(data, size);
        wxImage             img;
        if (!img.LoadFile(in, wxBITMAP_TYPE_JPEG))
            return;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            st->frame = img;
        }
        st->have_frame.store(true);
        wxQueueEvent(this, new wxCommandEvent(EVT_ORCA_IOS_CAMERA_FRAME));
    });

    st->client.set_error_fn([this, st](const std::string &reason) {
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            st->error = reason;
        }
        st->failed.store(true);
        st->have_frame.store(false);
        wxQueueEvent(this, new wxCommandEvent(EVT_ORCA_IOS_CAMERA_FRAME));
    });

    Slic3r::BambuLan::CameraClient::Config cfg;
    cfg.host        = st->host;
    cfg.access_code = st->access_code;
    st->client.start(cfg);

    BOOST_LOG_TRIVIAL(error) << "orca-ios-camera: streaming from " << st->host;
    Refresh(false);
}

void wxMediaCtrl2::Stop()
{
    CameraState *const st = static_cast<CameraState *>(m_player);
    if (st == nullptr)
        return;
    st->client.stop();
    st->have_frame.store(false);
    BOOST_LOG_TRIVIAL(error) << "orca-ios-camera: stopped after "
                             << st->client.frame_count() << " frames";
    Refresh(false);
}

void wxMediaCtrl2::SetIdleImage(wxString const & /*image*/) {}

wxMediaState wxMediaCtrl2::GetState() const
{
    CameraState *const st = static_cast<CameraState *>(m_player);
    if (st == nullptr || !st->client.is_running())
        return wxMEDIASTATE_STOPPED;
    // "Playing" only once a picture has actually arrived. MediaPlayCtrl uses
    // this to decide whether the stream is up, and a connection that
    // authenticated but produced nothing is not up.
    return st->have_frame.load() ? wxMEDIASTATE_PLAYING : wxMEDIASTATE_STOPPED;
}

wxSize wxMediaCtrl2::GetVideoSize() const
{
    CameraState *const st = static_cast<CameraState *>(m_player);
    if (st == nullptr || !st->have_frame.load())
        return wxSize(0, 0);
    std::lock_guard<std::mutex> lock(st->mutex);
    return st->frame.IsOk() ? wxSize(st->frame.GetWidth(), st->frame.GetHeight()) : wxSize(0, 0);
}
