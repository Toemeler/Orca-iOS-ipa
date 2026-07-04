///////////////////////////////////////////////////////////////////////////////
// Name:        wx/osx/iphone/filedlg.h
// Purpose:     wxFileDialog for wxOSX/iPhone, backed by UIDocumentPicker
//              (Orca-iOS-ipa). Avoids the generic wxFileCtrl/wxListCtrl stack.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////
#ifndef _WX_OSX_IPHONE_FILEDLG_H_
#define _WX_OSX_IPHONE_FILEDLG_H_

class WXDLLIMPEXP_CORE wxFileDialog : public wxFileDialogBase
{
public:
    wxFileDialog() { }
    wxFileDialog(wxWindow *parent,
                 const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                 const wxString& defaultDir = wxEmptyString,
                 const wxString& defaultFile = wxEmptyString,
                 const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                 long style = wxFD_DEFAULT_STYLE,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 const wxString& name = wxASCII_STR(wxFileDialogNameStr))
    {
        Create(parent, message, defaultDir, defaultFile, wildCard, style, pos, size, name);
    }

    bool Create(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                const wxString& defaultDir = wxEmptyString,
                const wxString& defaultFile = wxEmptyString,
                const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                long style = wxFD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxFileDialogNameStr));

    virtual int ShowModal() override;

    virtual void GetPaths(wxArrayString& paths) const override { paths = m_pickedPaths; }
    virtual void GetFilenames(wxArrayString& files) const override { files = m_pickedNames; }

private:
    wxArrayString m_pickedPaths;
    wxArrayString m_pickedNames;
    wxDECLARE_DYNAMIC_CLASS(wxFileDialog);
};

#endif // _WX_OSX_IPHONE_FILEDLG_H_
