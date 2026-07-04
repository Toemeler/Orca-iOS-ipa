// Orca-iOS-ipa: minimal wx/mediactrl.h stub. The iPhone port has no
// wxMediaCtrl backend (wxUSE_MEDIACTRL=0). Orca's wxMediaCtrl2 on __WXMAC__
// already derives from wxWindow (not wxMediaCtrl), and its AVFoundation-based
// .mm is excluded on iOS; the printer-camera video path is a step-5 task.
// This satisfies the includes + the enums/event that other TUs reference.
#ifndef _WX_MEDIACTRL_STUB_H_
#define _WX_MEDIACTRL_STUB_H_
#include "wx/control.h"
#include "wx/uri.h"

enum wxMediaState { wxMEDIASTATE_STOPPED, wxMEDIASTATE_PAUSED, wxMEDIASTATE_PLAYING };
enum wxMediaCtrlPlayerControls {
    wxMEDIACTRLPLAYERCONTROLS_NONE = 0,
    wxMEDIACTRLPLAYERCONTROLS_STEP = 1,
    wxMEDIACTRLPLAYERCONTROLS_VOLUME = 2,
    wxMEDIACTRLPLAYERCONTROLS_DEFAULT =
        wxMEDIACTRLPLAYERCONTROLS_STEP | wxMEDIACTRLPLAYERCONTROLS_VOLUME
};
#define wxMEDIABACKEND_DIRECTSHOW wxASCII_STR("wxAMMediaBackend")
#define wxMEDIABACKEND_QUICKTIME  wxASCII_STR("wxQTMediaBackend")
#define wxMEDIABACKEND_GSTREAMER  wxASCII_STR("wxGStreamerMediaBackend")

class WXDLLIMPEXP_CORE wxMediaEvent : public wxNotifyEvent
{
public:
    wxMediaEvent(wxEventType type = wxEVT_NULL, int id = 0)
        : wxNotifyEvent(type, id) {}
    wxEvent* Clone() const override { return new wxMediaEvent(*this); }
};

wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_LOADED, wxMediaEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_STOP, wxMediaEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_FINISHED, wxMediaEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_STATECHANGED, wxMediaEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_PLAY, wxMediaEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_MEDIA_PAUSE, wxMediaEvent);

#endif
