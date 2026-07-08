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
// Drag-and-drop entry points  (macOS impl: src/osx/cocoa/dnd.mm)
// ---------------------------------------------------------------------------
#if wxUSE_DRAG_AND_DROP

wxDropTarget::wxDropTarget(wxDataObject* dataObject)
    : wxDropTargetBase(dataObject)
{
}

wxDropSource::wxDropSource(wxWindow* WXUNUSED(win),
                           const wxCursorBundle& cursorCopy,
                           const wxCursorBundle& cursorMove,
                           const wxCursorBundle& cursorStop)
    : wxDropSourceBase(cursorCopy, cursorMove, cursorStop)
{
}

wxDragResult wxDropSource::DoDragDrop(int WXUNUSED(flags))
{
    return wxDragNone;
}

wxDropSource* wxDropSource::GetCurrentDropSource()
{
    return nullptr;
}

#endif // wxUSE_DRAG_AND_DROP
