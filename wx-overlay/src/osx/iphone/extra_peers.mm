/////////////////////////////////////////////////////////////////////////////
// iOS peer factories for controls the minimal wxOSX iPhone port did not
// implement but that Orca (and the now-enabled generic widgets: wxGrid's
// choice editor, wxSearchCtrl, etc.) reference. Each is backed by a plain
// UIView wrapped in the concrete wxWidgetIPhoneImpl (button.mm instantiates
// that class directly, so it is not abstract). This satisfies the linker and
// lays the control out; native appearance/behaviour is a later (step 5) polish.
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/window.h"
#include "wx/nonownedwnd.h"
#include "wx/statbox.h"
#include "wx/statline.h"
#include "wx/tglbtn.h"
#include "wx/radiobut.h"
#include "wx/spinbutt.h"
#include "wx/srchctrl.h"
#include "wx/combobox.h"

#include "wx/osx/private.h"

#import <UIKit/UIKit.h>

// Small helper: make a bare UIView-backed peer at the control's frame.
static wxWidgetImplType* wxIPhoneMakePlainPeer(wxWindowMac* wxpeer,
                                               const wxPoint& pos,
                                               const wxSize& size)
{
    CGRect r = wxOSXGetFrameForControl( wxpeer, pos, size );
    UIView* v = [[UIView alloc] initWithFrame:r];
    return new wxWidgetIPhoneImpl( wxpeer, v );
}

wxWidgetImplType* wxWidgetImpl::CreateStaticLine( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateGroupBox( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxString& WXUNUSED(label),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateSearchControl( wxSearchCtrl* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxString& WXUNUSED(content),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateRadioButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxString& WXUNUSED(label),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateToggleButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxString& WXUNUSED(label),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateSpinButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    wxInt32 WXUNUSED(value),
                                    wxInt32 WXUNUSED(minimum),
                                    wxInt32 WXUNUSED(maximum),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}

wxWidgetImplType* wxWidgetImpl::CreateComboBox( wxComboBox* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    wxMenu* WXUNUSED(menu),
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakePlainPeer( wxpeer, pos, size );
}
