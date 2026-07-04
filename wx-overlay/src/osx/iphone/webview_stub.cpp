// Orca-iOS-ipa: event-type definitions for the compile-only iPhone wxWebView stub.
// NON-FUNCTIONAL (see wx/webview.h override). Real WKWebView backend = step 5.
#include "wx/wxprec.h"
#if wxUSE_WEBVIEW
#include "wx/webview.h"
wxDEFINE_EVENT(wxEVT_WEBVIEW_NAVIGATING, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_NAVIGATED, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_LOADED, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_ERROR, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_NEWWINDOW, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_TITLE_CHANGED, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, wxWebViewEvent);
wxDEFINE_EVENT(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, wxWebViewEvent);
#endif
