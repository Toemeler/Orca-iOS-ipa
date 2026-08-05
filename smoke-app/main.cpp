// WxSmoke: minimal wxWidgets app for the Orca-iOS-ipa step-2 proof.
// Boots the wxOSX/iPhone port, shows native-ish widgets, and clears a
// wxGLCanvas via EAGL/GLES to prove context creation + present.
// License: AGPL-3.0.
#include <wx/wx.h>
#include <wx/glcanvas.h>

class SmokeCanvas : public wxGLCanvas
{
public:
    explicit SmokeCanvas(wxWindow *parent)
        : wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxSize(400, 300))
        , m_ctx(new wxGLContext(this))
    {
        Bind(wxEVT_PAINT, &SmokeCanvas::OnPaint, this);
    }
    ~SmokeCanvas() override { delete m_ctx; }

private:
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC dc(this);
        SetCurrent(*m_ctx);
        glClearColor(0.10f, 0.55f, 0.30f, 1.0f); // green = GL context alive
        glClear(GL_COLOR_BUFFER_BIT);
        SwapBuffers();
    }
    wxGLContext *m_ctx;
};

class SmokeFrame : public wxFrame
{
public:
    SmokeFrame() : wxFrame(nullptr, wxID_ANY, "wx on iPadOS")
    {
        auto *panel = new wxPanel(this);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(panel, wxID_ANY,
                       "wxWidgets iPhone port is alive (Orca-iOS-ipa step 2)"),
                   0, wxALL, 12);
        sizer->Add(new wxButton(panel, wxID_ANY, "A wxButton"), 0, wxALL, 12);
        sizer->Add(new SmokeCanvas(panel), 1, wxEXPAND | wxALL, 12);
        panel->SetSizer(sizer);
        Show();
    }
};

// probe_boot.mm - shared startup instrumentation, linked into this app too.
// The control has been the least informative part of the probe runs: it dies
// like the real thing and writes no log at all, so "the smoke app does not
// start either" was as much as could ever be said about it. It is also the
// smallest wx app there is - a frame, a button and a GL canvas - so whatever it
// says about the launch handshake is uncontaminated by widget choice.
extern "C" void probe_note_oninit(void);

class SmokeApp : public wxApp
{
public:
    bool OnInit() override
    {
        probe_note_oninit();
        new SmokeFrame();
        return true;
    }
};

wxIMPLEMENT_APP(SmokeApp);
