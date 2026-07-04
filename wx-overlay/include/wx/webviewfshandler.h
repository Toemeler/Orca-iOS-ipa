// Orca-iOS-ipa: satisfied by the webview stub. NON-FUNCTIONAL (step-5 backend).
#ifndef _WX_WEBVIEWFSHANDLER_STUB_H_
#define _WX_WEBVIEWFSHANDLER_STUB_H_
#include "wx/webview.h"
class WXDLLIMPEXP_CORE wxWebViewFSHandler : public wxWebViewHandler
{
public:
    wxWebViewFSHandler(const wxString& scheme) : wxWebViewHandler(scheme) {}
};
#endif
