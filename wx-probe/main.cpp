// WxProbe - does the wxOSX/iPhone port render the widgets Orca actually uses?
//
// Step 2 proved the port boots: a frame, a button and a wxGLCanvas cleared to
// green. That is the floor. Orca's MainFrame is built out of a much narrower and
// much heavier set - a book control holding the tabs, a wxDataViewCtrl for the
// object list, custom wxDC-painted widgets everywhere, and a GL canvas that
// renders through shaders rather than clearing. Any one of those failing on iOS
// costs a 55-minute Orca build to discover, and the failure looks identical to
// every other startup exit.
//
// This builds against the same cached wx prefix in about a minute and puts each
// of them on screen, one per tab, with the outcome written to stderr as it goes.
// A page that throws is caught and replaced by a label saying so, so one broken
// widget does not hide the state of the others.
//
// License: AGPL-3.0.
#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/dataview.h>
#include <wx/scrolwin.h>
#include <wx/glcanvas.h>
#include <wx/dcbuffer.h>
#include <wx/stdpaths.h>
#ifdef PROBE_WEBVIEW
#include <wx/webview.h>
#endif

#include <OpenGLES/ES2/gl.h>

#include <cstdio>

// Written to stderr *and* to a file the launch script will find. Run 1 came back
// with an empty sim-launch.log: --console-pty is not a reliable way to get an
// iOS app's stderr, and plain fprintf never reaches the system log either, so
// the interesting half of the probe would have been lost. The launch script
// pulls every *.log out of the app's data container, so writing one there is
// the path that cannot silently produce nothing.
static void report(const char* what, const char* how)
{
    std::fprintf(stderr, "WXPROBE %-22s %s\n", what, how);
    std::fflush(stderr);

    static FILE* f = nullptr;
    if (f == nullptr) {
        const wxString dir = wxStandardPaths::Get().GetDocumentsDir();
        f = std::fopen((dir + "/wxprobe.log").utf8_str().data(), "w");
    }
    if (f != nullptr) {
        std::fprintf(f, "WXPROBE %-22s %s\n", what, how);
        std::fflush(f);
    }
}

// ---------------------------------------------------------------- GL canvas
// Orca does not clear-and-present; it compiles shaders, uploads buffers and
// draws. On iOS the default framebuffer belongs to GLKit rather than being
// object 0, which is what patch 0329 is about - if that is wrong, the draw goes
// nowhere and this tab stays black while the others render.
class ProbeCanvas : public wxGLCanvas
{
public:
    explicit ProbeCanvas(wxWindow* parent)
        : wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize)
        , m_ctx(new wxGLContext(this))
    {
        Bind(wxEVT_PAINT, &ProbeCanvas::OnPaint, this);
    }
    ~ProbeCanvas() override { delete m_ctx; }

private:
    GLuint compile(GLenum type, const char* src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512] = {0};
            glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
            report("gl shader compile", log[0] ? log : "FAILED");
            return 0;
        }
        return s;
    }

    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        if (!SetCurrent(*m_ctx)) {
            report("gl SetCurrent", "FAILED");
            return;
        }
        if (!m_reported) {
            const GLubyte* ver = glGetString(GL_VERSION);
            const GLubyte* ren = glGetString(GL_RENDERER);
            report("gl version", ver ? (const char*) ver : "?");
            report("gl renderer", ren ? (const char*) ren : "?");
            // Which object the drawable framebuffer actually is. On iOS it is
            // whatever GLKit bound, not 0 - the number here is the thing patch
            // 0329 has to hand to Orca.
            GLint fbo = -1;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            report("gl default fbo", wxString::Format("%d", fbo).utf8_str().data());
        }

        const wxSize sz = GetClientSize();
        glViewport(0, 0, sz.x, sz.y);
        glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (m_prog == 0) {
            GLuint vs = compile(GL_VERTEX_SHADER,
                "attribute vec2 pos;\n"
                "attribute vec3 col;\n"
                "varying vec3 vcol;\n"
                "void main() { vcol = col; gl_Position = vec4(pos, 0.0, 1.0); }\n");
            GLuint fs = compile(GL_FRAGMENT_SHADER,
                "precision mediump float;\n"
                "varying vec3 vcol;\n"
                "void main() { gl_FragColor = vec4(vcol, 1.0); }\n");
            if (vs && fs) {
                m_prog = glCreateProgram();
                glAttachShader(m_prog, vs);
                glAttachShader(m_prog, fs);
                glBindAttribLocation(m_prog, 0, "pos");
                glBindAttribLocation(m_prog, 1, "col");
                glLinkProgram(m_prog);
                GLint ok = 0;
                glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
                report("gl program link", ok ? "ok" : "FAILED");
            }
        }

        if (m_prog) {
            static const GLfloat verts[] = {
                -0.8f, -0.7f,   -0.8f,  0.7f,   0.8f,  0.0f,
            };
            static const GLfloat cols[] = {
                 0.98f, 0.45f, 0.12f,   0.12f, 0.72f, 0.98f,   0.35f, 0.85f, 0.40f,
            };
            glUseProgram(m_prog);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, cols);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            const GLenum err = glGetError();
            if (!m_reported)
                report("gl draw", err == GL_NO_ERROR ? "ok" : "glGetError set");
        }

        SwapBuffers();
        if (!m_reported) {
            report("gl SwapBuffers", "returned");
            m_reported = true;
        }
    }

    wxGLContext* m_ctx;
    GLuint       m_prog     = 0;
    bool         m_reported = false;
};

// ------------------------------------------------------- custom-painted panel
// Most of Orca's chrome is not native controls; it is wxWindow subclasses that
// draw themselves with a wxDC. If text, lines and filled shapes do not appear
// here, none of Orca's sidebar or status bar will either.
class PaintedPanel : public wxPanel
{
public:
    explicit PaintedPanel(wxWindow* parent) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &PaintedPanel::OnPaint, this);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(wxColour(38, 40, 46)));
        dc.Clear();

        dc.SetPen(wxPen(wxColour(0x00, 0xAE, 0x42), 3));
        dc.SetBrush(wxBrush(wxColour(0x1F, 0x8A, 0x4C)));
        dc.DrawRoundedRectangle(20, 20, sz.x - 40, 90, 8);

        dc.SetTextForeground(*wxWHITE);
        dc.SetFont(wxFontInfo(20).Bold());
        dc.DrawText("wxDC drawing works", 40, 45);

        dc.SetFont(wxFontInfo(13));
        dc.DrawText("rounded rect + pen + brush + text + lines", 40, 78);

        dc.SetPen(wxPen(wxColour(0x66, 0x9E, 0xFF), 2));
        for (int i = 0; i < 12; ++i)
            dc.DrawLine(30 + i * 24, 140, 30 + i * 24, 140 + 10 * (i + 1));

        dc.SetBrush(wxBrush(wxColour(0xE8, 0x71, 0x1E)));
        dc.DrawCircle(sz.x / 2, 320, 46);

        if (!m_reported) {
            report("wxPaintDC", "painted");
            m_reported = true;
        }
    }
    bool m_reported = false;
};

// ------------------------------------------------------------------ the frame
class ProbeFrame : public wxFrame
{
public:
    ProbeFrame() : wxFrame(nullptr, wxID_ANY, "wx widget probe")
    {
        auto* book = new wxNotebook(this, wxID_ANY);
        report("wxNotebook", "constructed");

        book->AddPage(MakeControls(book), "Controls", true);
        book->AddPage(MakeDataView(book), "DataView");
        book->AddPage(new PaintedPanel(book), "Custom paint");
        book->AddPage(MakeGL(book), "OpenGL ES");
#ifdef PROBE_WEBVIEW
        book->AddPage(MakeWebView(book), "WebView");
#endif

        report("pages added", "ok");
        Show();
        report("frame Show()", "returned");
    }

private:
    wxWindow* MakeControls(wxWindow* parent)
    {
        auto* scroller = new wxScrolledWindow(parent);
        scroller->SetScrollRate(0, 12);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(scroller, wxID_ANY,
                       "Orca's dialogs are built from these."),
                   0, wxALL, 12);
        sizer->Add(new wxButton(scroller, wxID_ANY, "wxButton"), 0, wxALL, 8);
        sizer->Add(new wxTextCtrl(scroller, wxID_ANY, "wxTextCtrl"), 0, wxALL | wxEXPAND, 8);
        sizer->Add(new wxCheckBox(scroller, wxID_ANY, "wxCheckBox"), 0, wxALL, 8);
        wxString choices[] = {"wxChoice one", "two", "three"};
        sizer->Add(new wxChoice(scroller, wxID_ANY, wxDefaultPosition, wxDefaultSize, 3, choices),
                   0, wxALL, 8);
        sizer->Add(new wxSlider(scroller, wxID_ANY, 40, 0, 100), 0, wxALL | wxEXPAND, 8);
        sizer->Add(new wxGauge(scroller, wxID_ANY, 100), 0, wxALL | wxEXPAND, 8);
        scroller->SetSizer(sizer);
        report("basic controls", "constructed");
        return scroller;
    }

    // The object list in Orca's sidebar is a wxDataViewCtrl, and patch 0310
    // already had to stop it registering a native renderer on iOS. Whether it
    // draws at all is unknown until something puts one on screen.
    wxWindow* MakeDataView(wxWindow* parent)
    {
        auto* panel = new wxPanel(parent);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        try {
            auto* dv = new wxDataViewListCtrl(panel, wxID_ANY);
            dv->AppendTextColumn("Object", wxDATAVIEW_CELL_INERT, 260);
            dv->AppendTextColumn("Extruder", wxDATAVIEW_CELL_INERT, 120);
            for (int i = 1; i <= 6; ++i) {
                wxVector<wxVariant> row;
                row.push_back(wxVariant(wxString::Format("part_%d.stl", i)));
                row.push_back(wxVariant(wxString::Format("%d", 1 + (i % 2))));
                dv->AppendItem(row);
            }
            sizer->Add(dv, 1, wxEXPAND | wxALL, 10);
            report("wxDataViewListCtrl", "constructed + 6 rows");
        } catch (const std::exception& e) {
            sizer->Add(new wxStaticText(panel, wxID_ANY,
                           wxString("wxDataViewListCtrl threw: ") + e.what()),
                       0, wxALL, 12);
            report("wxDataViewListCtrl", "THREW");
        }
        panel->SetSizer(sizer);
        return panel;
    }

#ifdef PROBE_WEBVIEW
    // Orca's MainFrame builds web-view panels for the home page and the printer
    // page, and step-2 patch 0210 compiled wx's WKWebView backend for the iPhone
    // port - but no wxWebView has ever been constructed on iOS here, and one
    // that throws during MainFrame construction is indistinguishable from any
    // other startup exit.
    wxWindow* MakeWebView(wxWindow* parent)
    {
        auto* panel = new wxPanel(parent);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        wxWebView* wv = wxWebView::New();
        if (wv == nullptr) {
            report("wxWebView::New", "returned null");
            sizer->Add(new wxStaticText(panel, wxID_ANY, "wxWebView::New() returned null"),
                       0, wxALL, 12);
        } else if (!wv->Create(panel, wxID_ANY, "about:blank")) {
            report("wxWebView::Create", "FAILED");
            sizer->Add(new wxStaticText(panel, wxID_ANY, "wxWebView::Create() failed"),
                       0, wxALL, 12);
        } else {
            // A data: page rather than a network fetch: this is about whether a
            // WKWebView appears and paints, not about connectivity.
            wv->SetPage("<html><body style='background:#12331f;color:#eaeaea;"
                        "font:600 34px -apple-system'>"
                        "<p>wxWebView renders on iOS</p></body></html>", "");
            sizer->Add(wv, 1, wxEXPAND | wxALL, 6);
            report("wxWebView", "created + SetPage");
        }
        panel->SetSizer(sizer);
        return panel;
    }
#endif

    wxWindow* MakeGL(wxWindow* parent)
    {
        auto* panel = new wxPanel(parent);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new ProbeCanvas(panel), 1, wxEXPAND | wxALL, 6);
        panel->SetSizer(sizer);
        report("wxGLCanvas", "constructed");
        return panel;
    }
};

class ProbeApp : public wxApp
{
public:
    bool OnInit() override
    {
        report("OnInit", "entered");
        new ProbeFrame();
        report("OnInit", "returning true");
        return true;
    }
};

wxIMPLEMENT_APP(ProbeApp);
