///////////////////////////////////////////////////////////////////////////////
// Name:        wx/osx/iphone/dirdlg.h
// Purpose:     wxDirDialog for wxOSX/iPhone, backed by UIDocumentPicker in
//              folder mode (Orca-iOS-ipa). Avoids the generic wxDirCtrl/
//              wxTreeCtrl stack that src/generic/dirdlgg.cpp would drag in.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////
#ifndef _WX_OSX_IPHONE_DIRDLG_H_
#define _WX_OSX_IPHONE_DIRDLG_H_

class WXDLLIMPEXP_CORE wxDirDialog : public wxDirDialogBase
{
public:
    wxDirDialog() { }
    wxDirDialog(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr))
    {
        Create(parent, message, defaultPath, style, pos, size, name);
    }

    // Shadows (non-virtual) wxDirDialogBase::Create on purpose: we only stash
    // state and never create a native dialog window, exactly like the iphone
    // wxFileDialog shim.
    bool Create(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr));

    virtual int ShowModal() override;

    wxDECLARE_CLASS(wxDirDialog);
};

#endif // _WX_OSX_IPHONE_DIRDLG_H_
