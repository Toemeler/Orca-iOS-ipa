///////////////////////////////////////////////////////////////////////////////
// Name:        wx/osx/iphone/webview_stub.h  (installed as wx/webview.h override)
// Purpose:     Compile-only wxWebView stub for wxOSX/iPhone (Orca-iOS-ipa).
//              NON-FUNCTIONAL: lets libslic3r_gui compile so the app can be
//              built and launched; the real WKWebView-backed implementation is
//              a step-5 task (the macOS backend src/osx/webview_webkit.mm is
//              already WKWebView-based and can be ported to UIView on iOS).
//              Device pages / login / guides that rely on the webview will be
//              inert until then, but the rest of the GUI works.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////
#ifndef _WX_OSX_IPHONE_WEBVIEW_STUB_H_
#define _WX_OSX_IPHONE_WEBVIEW_STUB_H_

#include "wx/control.h"
#include "wx/event.h"
#include "wx/sharedptr.h"

#define wxWebViewBackendDefault    wxASCII_STR("wxWebViewIE")
#define wxWebViewBackendWebKit     wxASCII_STR("wxWebViewWebKit")
#define wxWebViewBackendEdge       wxASCII_STR("wxWebViewEdge")

enum wxWebViewZoom { wxWEBVIEW_ZOOM_TINY, wxWEBVIEW_ZOOM_SMALL,
                     wxWEBVIEW_ZOOM_MEDIUM, wxWEBVIEW_ZOOM_LARGE,
                     wxWEBVIEW_ZOOM_LARGEST };
enum wxWebViewZoomType { wxWEBVIEW_ZOOM_TYPE_LAYOUT, wxWEBVIEW_ZOOM_TYPE_TEXT };
enum wxWebViewNavigationActionFlags { wxWEBVIEW_NAV_ACTION_NONE,
                                      wxWEBVIEW_NAV_ACTION_USER,
                                      wxWEBVIEW_NAV_ACTION_OTHER };
enum wxWebViewNavigationError { wxWEBVIEW_NAV_ERR_CONNECTION,
                                wxWEBVIEW_NAV_ERR_CERTIFICATE,
                                wxWEBVIEW_NAV_ERR_AUTH,
                                wxWEBVIEW_NAV_ERR_SECURITY,
                                wxWEBVIEW_NAV_ERR_NOT_FOUND,
                                wxWEBVIEW_NAV_ERR_REQUEST,
                                wxWEBVIEW_NAV_ERR_USER_CANCELLED,
                                wxWEBVIEW_NAV_ERR_OTHER };

class WXDLLIMPEXP_CORE wxWebViewHandler
{
public:
    wxWebViewHandler(const wxString& scheme) : m_scheme(scheme) {}
    virtual ~wxWebViewHandler() {}
    virtual wxString GetName() const { return m_scheme; }
private:
    wxString m_scheme;
};

class WXDLLIMPEXP_CORE wxWebView : public wxControl
{
public:
    wxWebView() {}

    static wxWebView* New(wxWindow* = nullptr, wxWindowID = wxID_ANY,
                          const wxString& = wxEmptyString,
                          const wxPoint& = wxDefaultPosition,
                          const wxSize& = wxDefaultSize,
                          const wxString& = wxWebViewBackendDefault,
                          long = 0, const wxString& = wxEmptyString)
    { return nullptr; }

    virtual void LoadURL(const wxString&) {}
    virtual void SetPage(const wxString&, const wxString&) {}
    virtual wxString GetCurrentURL() const { return wxEmptyString; }
    virtual wxString GetCurrentTitle() const { return wxEmptyString; }
    virtual void Reload(wxWebViewReloadFlags = 0) {}
    virtual void Stop() {}
    virtual bool CanGoBack() const { return false; }
    virtual bool CanGoForward() const { return false; }
    virtual void GoBack() {}
    virtual void GoForward() {}
    virtual bool RunScript(const wxString&, wxString* = nullptr) const { return false; }
    virtual void AddScriptMessageHandler(const wxString&) {}
    virtual void RemoveScriptMessageHandler(const wxString&) {}
    virtual void EnableContextMenu(bool = true) {}
    virtual void EnableAccessToDevTools(bool = true) {}
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler>) {}
    virtual void* GetNativeBackend() const { return nullptr; }
    virtual bool AddUserScript(const wxString&, int = 0) { return false; }
};

typedef int wxWebViewReloadFlags;

class WXDLLIMPEXP_CORE wxWebViewEvent : public wxNotifyEvent
{
public:
    wxWebViewEvent() {}
    wxWebViewEvent(wxEventType type, int id, const wxString& url,
                   const wxString& target,
                   wxWebViewNavigationActionFlags flags = wxWEBVIEW_NAV_ACTION_NONE)
        : wxNotifyEvent(type, id), m_url(url), m_target(target), m_actionFlags(flags) {}
    const wxString& GetURL() const { return m_url; }
    const wxString& GetTarget() const { return m_target; }
    const wxString& GetString() const { return m_url; }
    wxString GetMessageHandler() const { return wxEmptyString; }
    int GetNavigationAction() const { return m_actionFlags; }
    wxEvent* Clone() const override { return new wxWebViewEvent(*this); }
private:
    wxString m_url, m_target;
    int m_actionFlags = wxWEBVIEW_NAV_ACTION_NONE;
};

// Event type decls (defined in the stub .cpp).
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_NAVIGATING, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_NAVIGATED, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_LOADED, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_ERROR, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_NEWWINDOW, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_TITLE_CHANGED, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_FULLSCREEN_CHANGED, wxWebViewEvent);
wxDECLARE_EXPORTED_EVENT(WXDLLIMPEXP_CORE, wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, wxWebViewEvent);

#define EVT_WEBVIEW_NAVIGATING(id, fn)  wx__DECLARE_EVT1(wxEVT_WEBVIEW_NAVIGATING, id, &fn)
#define EVT_WEBVIEW_NAVIGATED(id, fn)   wx__DECLARE_EVT1(wxEVT_WEBVIEW_NAVIGATED, id, &fn)
#define EVT_WEBVIEW_LOADED(id, fn)      wx__DECLARE_EVT1(wxEVT_WEBVIEW_LOADED, id, &fn)
#define EVT_WEBVIEW_ERROR(id, fn)       wx__DECLARE_EVT1(wxEVT_WEBVIEW_ERROR, id, &fn)

class WXDLLIMPEXP_CORE wxWebViewArchiveHandler : public wxWebViewHandler
{
public:
    wxWebViewArchiveHandler(const wxString& scheme) : wxWebViewHandler(scheme) {}
};

#endif // _WX_OSX_IPHONE_WEBVIEW_STUB_H_
