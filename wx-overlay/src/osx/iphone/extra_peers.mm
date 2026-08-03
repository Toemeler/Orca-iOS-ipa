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
#include "wx/arrstr.h"

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

// tglbtn_osx.cpp routes bitmap toggle buttons to this factory rather than
// CreateToggleButton; the cocoa implementation is in cocoa/tglbtn.mm.
wxWidgetImplType* wxWidgetImpl::CreateBitmapToggleButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxBitmapBundle& WXUNUSED(label),
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

// wxComboBox is the one control here that cannot use the plain peer. Every
// list operation in combobox_osx.cpp goes through GetComboPeer(), which is
// dynamic_cast<wxComboWidgetImpl*>(GetPeer()), and the result is dereferenced
// without a null check -- a plain peer would return nullptr and crash as soon
// as a combobox is populated. So the iPhone combo peer derives from
// wxComboWidgetImpl (same shape as the Cocoa wxNSComboBoxControl) and keeps
// the items itself, which makes wxComboBox correct as a data container.
// Presenting them as a dropdown is step-5 UI work (UIPickerView); until then
// Popup/Dismiss inherit the base no-ops.
class wxIPhoneComboBoxPeer : public wxWidgetIPhoneImpl, public wxComboWidgetImpl
{
public:
    wxIPhoneComboBoxPeer( wxWindowMac* wxpeer, UIView* v )
        : wxWidgetIPhoneImpl( wxpeer, v ) {}

    int GetSelectedItem() const override { return m_selection; }
    void SetSelectedItem(int item) override { m_selection = item; }

    int GetNumberOfItems() const override
        { return static_cast<int>(m_items.GetCount()); }

    void InsertItem(int pos, const wxString& item) override
    {
        m_items.Insert( item, pos );
        if ( m_selection >= pos )
            ++m_selection;
    }

    void RemoveItem(int pos) override
    {
        m_items.RemoveAt( pos );
        if ( m_selection == pos )
            m_selection = wxNOT_FOUND;
        else if ( m_selection > pos )
            --m_selection;
    }

    void Clear() override
    {
        m_items.Clear();
        m_selection = wxNOT_FOUND;
    }

    wxString GetStringAtIndex(int pos) const override
    {
        return ( pos >= 0 && pos < static_cast<int>(m_items.GetCount()) )
                    ? m_items[pos] : wxString();
    }

    int FindString(const wxString& text) const override
        { return m_items.Index( text ); }

private:
    wxArrayString m_items;
    int           m_selection = wxNOT_FOUND;
};

// Declared unconditionally in wx/osx/combobox.h but defined only in
// cocoa/combobox.mm. Mirrors that implementation, measuring through the window
// rather than a wxInfoDC so no DC has to exist before the control is shown.
wxSize wxComboBox::DoGetBestSize() const
{
    int lbWidth = GetCount() > 0 ? 20 : 100; // some defaults
    const wxSize baseSize = wxWindow::DoGetBestSize();
    const int lbHeight = baseSize.y;

    for ( unsigned int i = 0; i < GetCount(); ++i )
    {
        int width = 0, height = 0;
        GetTextExtent(GetString(i), &width, &height);
        lbWidth = wxMax(lbWidth, width);
    }

    // Add room for the popup arrow, as the cocoa peer does.
    lbWidth += 2 * lbHeight;

    return wxSize(lbWidth, lbHeight);
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
    CGRect r = wxOSXGetFrameForControl( wxpeer, pos, size );
    UIView* v = [[UIView alloc] initWithFrame:r];
    return new wxIPhoneComboBoxPeer( wxpeer, v );
}
