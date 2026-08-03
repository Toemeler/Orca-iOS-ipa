// Orca Viewer — a small native iPadOS STL viewer built on the wxWidgets
// iPhone port, using the same OpenGL ES canvas path OrcaSlicer itself needs.
//
// Objective-C++ for two reasons only: reaching the GLKView behind the wx
// canvas to give it a depth buffer, and asking UIKit for the Documents
// directory. Everything else is plain wx and OpenGL ES 1.1 fixed-function,
// which is the context the stock wx iPhone port creates.
//
// Drop .stl files into the app's folder in the Files app and they appear in
// the picker. License: AGPL-3.0.

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <wx/dir.h>

#import <GLKit/GLKit.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cmath>

#include "stl.h"

// ---------------------------------------------------------------------------

static wxString DocumentsDir()
{
    NSArray* dirs = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    if ( dirs.count == 0 ) return wxEmptyString;
    return wxString::FromUTF8([[dirs objectAtIndex:0] UTF8String]);
}

// ---------------------------------------------------------------------------

class ViewerCanvas : public wxGLCanvas
{
public:
    explicit ViewerCanvas(wxWindow* parent)
        : wxGLCanvas(parent, wxID_ANY, nullptr)
        , m_ctx(new wxGLContext(this))
        , m_timer(this)
    {
        // The wx iPhone port leaves GLKView at its default of "no depth
        // buffer", which would render a solid mesh as a jumble of triangles in
        // arbitrary order. Ask for one before the drawable is first created.
        if ( GLKView* v = (GLKView*) GetHandle() )
            v.drawableDepthFormat = GLKViewDrawableDepthFormat24;

        SetMesh(makeCube());

        Bind(wxEVT_PAINT,       &ViewerCanvas::OnPaint,   this);
        Bind(wxEVT_LEFT_DOWN,   &ViewerCanvas::OnDown,    this);
        Bind(wxEVT_LEFT_UP,     &ViewerCanvas::OnUp,      this);
        Bind(wxEVT_MOTION,      &ViewerCanvas::OnMotion,  this);
        Bind(wxEVT_TIMER,       &ViewerCanvas::OnTimer,   this);

        m_timer.Start(33); // ~30fps idle spin
    }

    ~ViewerCanvas() override { delete m_ctx; }

    void SetMesh(Mesh m)
    {
        m_mesh = std::move(m);
        m_yaw = 30.0f;
        m_pitch = -25.0f;
        m_zoom = 1.0f;
        Refresh();
    }

    const Mesh& GetMesh() const { return m_mesh; }

    void Zoom(float factor)
    {
        m_zoom = std::min(8.0f, std::max(0.15f, m_zoom * factor));
        Refresh();
    }

    void ResetView()
    {
        m_yaw = 30.0f; m_pitch = -25.0f; m_zoom = 1.0f;
        Refresh();
    }

    void ToggleSpin()
    {
        m_spin = !m_spin;
        Refresh();
    }

    bool Spinning() const { return m_spin; }

private:
    void OnTimer(wxTimerEvent&)
    {
        if ( m_spin && !m_dragging ) { m_yaw += 0.6f; Refresh(); }
    }

    void OnDown(wxMouseEvent& e)
    {
        m_dragging = true;
        m_last = e.GetPosition();
        if ( !HasCapture() ) CaptureMouse();
    }

    void OnUp(wxMouseEvent&)
    {
        m_dragging = false;
        if ( HasCapture() ) ReleaseMouse();
    }

    void OnMotion(wxMouseEvent& e)
    {
        if ( !m_dragging || !e.Dragging() ) return;
        const wxPoint p = e.GetPosition();
        m_yaw   += (p.x - m_last.x) * 0.5f;
        m_pitch += (p.y - m_last.y) * 0.5f;
        m_pitch = std::min(89.0f, std::max(-89.0f, m_pitch));
        m_last = p;
        Refresh();
    }

    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        SetCurrent(*m_ctx);

        const double scale = GetContentScaleFactor();
        const wxSize sz = GetClientSize();
        const int w = std::max(1, int(sz.x * scale));
        const int h = std::max(1, int(sz.y * scale));

        glViewport(0, 0, w, h);
        glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_NORMALIZE);
        glShadeModel(GL_FLAT); // per-facet normals: faceted is the honest look

        const float aspect = float(w) / float(h);
        const float n = 0.1f, f = 100.0f;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-aspect * n * 0.5f, aspect * n * 0.5f,
                   -n * 0.5f, n * 0.5f, n, f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Directional light set under the identity modelview, so it stays put
        // relative to the camera rather than swinging around with the model.
        const GLfloat lpos[4] = { 0.35f, 0.55f, 1.0f, 0.0f };
        const GLfloat lamb[4] = { 0.24f, 0.25f, 0.30f, 1.0f };
        const GLfloat ldif[4] = { 0.95f, 0.95f, 0.92f, 1.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lpos);
        glLightfv(GL_LIGHT0, GL_AMBIENT,  lamb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  ldif);

        const GLfloat mat[4] = { 0.20f, 0.72f, 0.52f, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat);

        if ( m_mesh.ok && m_mesh.triangles )
        {
            // Frame the model: scale its largest dimension to a fixed size so
            // a 5 mm part and a 250 mm part both arrive on screen usable.
            const float extent = std::max({ m_mesh.size(0),
                                            m_mesh.size(1),
                                            m_mesh.size(2), 1e-6f });
            const float fit = 1.0f / extent;

            glTranslatef(0.0f, 0.0f, -2.4f / m_zoom);
            glRotatef(m_pitch, 1.0f, 0.0f, 0.0f);
            glRotatef(m_yaw,   0.0f, 1.0f, 0.0f);
            glScalef(fit, fit, fit);
            glTranslatef(-m_mesh.centre(0), -m_mesh.centre(1), -m_mesh.centre(2));

            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, m_mesh.verts.data());
            glNormalPointer(GL_FLOAT, 0, m_mesh.normals.data());
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(m_mesh.triangles * 3));
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
        }

        SwapBuffers();
    }

    Mesh          m_mesh;
    wxGLContext*  m_ctx;
    wxTimer       m_timer;
    wxPoint       m_last;
    float         m_yaw = 30.0f;
    float         m_pitch = -25.0f;
    float         m_zoom = 1.0f;
    bool          m_dragging = false;
    bool          m_spin = true;
};

// ---------------------------------------------------------------------------

class ViewerFrame : public wxFrame
{
public:
    ViewerFrame() : wxFrame(nullptr, wxID_ANY, "Orca Viewer")
    {
        auto* panel = new wxPanel(this);
        auto* root  = new wxBoxSizer(wxVERTICAL);

        auto* bar = new wxBoxSizer(wxHORIZONTAL);
        m_choice = new wxChoice(panel, wxID_ANY);
        auto* reload = new wxButton(panel, wxID_ANY, "Reload");
        auto* zoomIn = new wxButton(panel, wxID_ANY, "+");
        auto* zoomOut= new wxButton(panel, wxID_ANY, "-");
        auto* reset  = new wxButton(panel, wxID_ANY, "Reset");
        m_spinBtn    = new wxButton(panel, wxID_ANY, "Pause");

        bar->Add(m_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bar->Add(reload,  0, wxRIGHT, 6);
        bar->Add(zoomOut, 0, wxRIGHT, 6);
        bar->Add(zoomIn,  0, wxRIGHT, 6);
        bar->Add(reset,   0, wxRIGHT, 6);
        bar->Add(m_spinBtn, 0);

        m_info = new wxStaticText(panel, wxID_ANY, wxEmptyString);
        m_canvas = new ViewerCanvas(panel);

        root->Add(bar,      0, wxEXPAND | wxALL, 10);
        root->Add(m_info,   0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        root->Add(m_canvas, 1, wxEXPAND | wxALL, 10);
        panel->SetSizer(root);

        m_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { LoadSelected(); });
        reload->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { Rescan(); });
        zoomIn->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { m_canvas->Zoom(1.25f); });
        zoomOut->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_canvas->Zoom(0.8f); });
        reset->Bind(wxEVT_BUTTON,   [this](wxCommandEvent&) { m_canvas->ResetView(); });
        m_spinBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_canvas->ToggleSpin();
            m_spinBtn->SetLabel(m_canvas->Spinning() ? "Pause" : "Spin");
        });

        Rescan();
        Show();
    }

private:
    void Rescan()
    {
        m_files.clear();
        m_choice->Clear();
        m_choice->Append("Built-in 20 mm cube");

        const wxString docs = DocumentsDir();
        if ( !docs.empty() )
        {
            wxDir dir(docs);
            if ( dir.IsOpened() )
            {
                wxString name;
                bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES);
                while ( more )
                {
                    if ( name.Lower().EndsWith(".stl") )
                    {
                        m_files.push_back(docs + "/" + name);
                        m_choice->Append(name);
                    }
                    more = dir.GetNext(&name);
                }
            }
        }

        m_choice->SetSelection(0);
        LoadSelected();
    }

    void LoadSelected()
    {
        const int sel = m_choice->GetSelection();
        if ( sel <= 0 )
        {
            m_canvas->SetMesh(makeCube());
            Describe("Built-in cube");
            return;
        }

        const size_t idx = size_t(sel - 1);
        if ( idx >= m_files.size() ) return;

        Mesh m = loadSTL(std::string(m_files[idx].utf8_str()));
        if ( !m.ok )
        {
            m_info->SetLabel(wxString::Format("Could not read %s — %s",
                                              m_choice->GetString(sel),
                                              wxString::FromUTF8(m.error.c_str())));
            return;
        }
        m_canvas->SetMesh(std::move(m));
        Describe(m_choice->GetString(sel));
    }

    void Describe(const wxString& label)
    {
        const Mesh& m = m_canvas->GetMesh();
        m_info->SetLabel(wxString::Format(
            "%s   %lu triangles   %.1f x %.1f x %.1f mm",
            label, (unsigned long) m.triangles,
            m.size(0), m.size(1), m.size(2)));
        Layout();
    }

    wxChoice*             m_choice = nullptr;
    wxStaticText*         m_info = nullptr;
    wxButton*             m_spinBtn = nullptr;
    ViewerCanvas*         m_canvas = nullptr;
    std::vector<wxString> m_files;
};

// ---------------------------------------------------------------------------

class ViewerApp : public wxApp
{
public:
    bool OnInit() override
    {
        new ViewerFrame();
        return true;
    }
};

wxIMPLEMENT_APP(ViewerApp);
