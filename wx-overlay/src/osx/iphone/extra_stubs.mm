/////////////////////////////////////////////////////////////////////////////
// iOS stubs for wx features whose macOS implementation lives in AppKit-only
// sources (cocoa/tooltip.mm, and the NSPasteboard/NSDragging parts of
// cocoa/dnd.mm) that cannot compile against the iPhoneSimulator SDK.
//
// Everything here is inert-but-linkable: the app links and does not crash as
// long as these paths are not exercised (tooltips, clipboard paste and
// drag-and-drop are not on the launch path). Real UIPasteboard / iOS drag
// support is a step-5 item.
//
// NOTE ON SPLIT CLASSES: wxDropSource/wxDropTarget are *co-defined* with the
// un-gated src/osx/dnd_osx.cpp, which supplies their virtual methods and the
// wxDropSource destructor (the vtable/typeinfo anchor). We therefore provide
// ONLY the members dnd_osx.cpp does not: the constructors, DoDragDrop and
// GetCurrentDropSource (all originally in cocoa/dnd.mm). Do not add the
// destructor here or the link will see a duplicate symbol.
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/window.h"
#include "wx/tooltip.h"
#include "wx/clipbrd.h"
#include "wx/dnd.h"
#include "wx/dataobj.h"

// ---------------------------------------------------------------------------
// wxToolTip  (macOS impl: src/osx/cocoa/tooltip.mm)
// ---------------------------------------------------------------------------
#if wxUSE_TOOLTIPS

wxIMPLEMENT_ABSTRACT_CLASS(wxToolTip, wxObject);

wxToolTip::wxToolTip(const wxString& tip)
    : m_text(tip), m_window(nullptr)
{
}

wxToolTip::~wxToolTip()
{
}

void wxToolTip::SetTip(const wxString& tip)   { m_text = tip; }
void wxToolTip::SetWindow(wxWindow* win)       { m_window = win; }

void wxToolTip::Enable(bool WXUNUSED(flag))                     {}
void wxToolTip::SetDelay(long WXUNUSED(ms))                     {}
void wxToolTip::SetAutoPop(long WXUNUSED(ms))                   {}
void wxToolTip::SetReshow(long WXUNUSED(ms))                    {}
void wxToolTip::NotifyWindowDelete(WXHWND WXUNUSED(win))        {}
void wxToolTip::RelayEvent(wxWindow* WXUNUSED(win),
                           wxMouseEvent& WXUNUSED(event))       {}
void wxToolTip::RemoveToolTips()                               {}

#endif // wxUSE_TOOLTIPS

// ---------------------------------------------------------------------------
// wxClipboard  (macOS impl: src/osx/carbon/clipbrd.cpp, which pulls in the
// AppKit wxOSXPasteboard from cocoa/dnd.mm -- so we stub the class instead of
// un-gating clipbrd.cpp).
// ---------------------------------------------------------------------------
#if wxUSE_CLIPBOARD

wxIMPLEMENT_DYNAMIC_CLASS(wxClipboard, wxClipboardBase);

wxClipboard::wxClipboard()
{
    m_open = false;
    m_data = nullptr;
}

wxClipboard::~wxClipboard()
{
}

bool wxClipboard::Open()                                    { return false; }
void wxClipboard::Close()                                   {}
bool wxClipboard::IsOpened() const                          { return false; }
bool wxClipboard::SetData( wxDataObject* WXUNUSED(data) )    { return false; }
bool wxClipboard::AddData( wxDataObject* WXUNUSED(data) )    { return false; }
bool wxClipboard::IsSupported( const wxDataFormat& WXUNUSED(format) ) { return false; }
bool wxClipboard::GetData( wxDataObject& WXUNUSED(data) )    { return false; }
void wxClipboard::Clear()                                   {}
bool wxClipboard::Flush()                                   { return false; }

#endif // wxUSE_CLIPBOARD

// ---------------------------------------------------------------------------
// wxDataFormat / wxDataObject / wxBitmapDataObject / wxFileDataObject
// (macOS impl: src/osx/carbon/dataobj.cpp, which cannot compile against the
// iOS 18.5 SDK: it uses the removed Carbon kUTType* constants and the
// Pasteboard-Manager OSXPasteboard type). We stub the class members that Orca
// and the now-enabled generic widgets reference, with complete vtables.
// ---------------------------------------------------------------------------
#if wxUSE_DATAOBJ

// wxDataFormat is a plain value type (no vtable); m_type/m_format are its only
// members. NativeFormat is CFStringRef; the stub keeps it null (inert).
wxDataFormat::wxDataFormat()                    : m_type(wxDF_INVALID), m_format(NULL) {}
wxDataFormat::wxDataFormat(wxDataFormatId vType): m_type(vType),        m_format(NULL) {}
wxDataFormat::wxDataFormat(const wxDataFormat& r): m_type(r.m_type),    m_format(r.m_format) {}
wxDataFormat::wxDataFormat(const wxString& WXUNUSED(id)) : m_type(wxDF_PRIVATE), m_format(NULL) {}
wxDataFormat::~wxDataFormat() {}
bool wxDataFormat::operator==(const wxDataFormat& f) const { return m_type == f.m_type; }
wxDataFormat& wxDataFormat::operator=(const wxDataFormat& f)
{
    m_type = f.m_type;
    m_format = f.m_format;
    return *this;
}

// wxDataObject is abstract; defining its two non-inline virtuals emits its
// vtable/typeinfo (the base's pure virtuals remain __cxa_pure_virtual).
wxDataObject::wxDataObject() {}
bool wxDataObject::IsSupportedFormat(const wxDataFormat& WXUNUSED(format),
                                     Direction WXUNUSED(dir)) const { return false; }
void wxDataObject::AddSupportedTypes(CFMutableArrayRef WXUNUSED(cfarray),
                                     Direction WXUNUSED(dir)) const {}

// Concrete data objects: stub the non-inline osx virtual overrides so their
// vtables link (the format-qualified overloads are inline in the header).
wxBitmapDataObject::wxBitmapDataObject() {}
wxBitmapDataObject::~wxBitmapDataObject() {}
size_t wxBitmapDataObject::GetDataSize() const { return 0; }
bool   wxBitmapDataObject::GetDataHere(void* WXUNUSED(buf)) const { return false; }
bool   wxBitmapDataObject::SetData(size_t WXUNUSED(len), const void* WXUNUSED(buf)) { return false; }

size_t wxFileDataObject::GetDataSize() const { return 0; }
bool   wxFileDataObject::GetDataHere(void* WXUNUSED(buf)) const { return false; }
bool   wxFileDataObject::SetData(size_t WXUNUSED(len), const void* WXUNUSED(buf)) { return false; }

#endif // wxUSE_DATAOBJ

// ---------------------------------------------------------------------------
// Drag-and-drop  (macOS impl: src/osx/dnd_osx.cpp + cocoa/dnd.mm). dnd_osx.cpp
// is co-dependent on the un-compilable carbon/dataobj.cpp and on the AppKit
// pasteboard, so we stub wxDropTarget/wxDropSource in full here instead of
// un-gating it. Every non-inline virtual is defined so both vtables link.
// ---------------------------------------------------------------------------
#if wxUSE_DRAG_AND_DROP

wxDropTarget::wxDropTarget(wxDataObject* dataObject)
    : wxDropTargetBase(dataObject), m_currentDragPasteboard(nullptr)
{
}

wxDragResult wxDropTarget::OnDragOver(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                      wxDragResult def)              { return def; }
bool         wxDropTarget::OnDrop(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y)) { return false; }
wxDragResult wxDropTarget::OnData(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                  wxDragResult WXUNUSED(def))        { return wxDragNone; }
bool         wxDropTarget::GetData()                                { return false; }
wxDataFormat wxDropTarget::GetMatchingPair()                        { return wxDataFormat(); }

wxDropSource::wxDropSource(wxWindow* win,
                           const wxCursorBundle& cursorCopy,
                           const wxCursorBundle& cursorMove,
                           const wxCursorBundle& cursorStop)
    : wxDropSourceBase(cursorCopy, cursorMove, cursorStop),
      m_window(win), m_currentDragPasteboard(nullptr)
{
}

wxDropSource::~wxDropSource() {}

wxDragResult wxDropSource::DoDragDrop(int WXUNUSED(flags))          { return wxDragNone; }

wxDropSource* wxDropSource::GetCurrentDropSource()                  { return nullptr; }

#endif // wxUSE_DRAG_AND_DROP
