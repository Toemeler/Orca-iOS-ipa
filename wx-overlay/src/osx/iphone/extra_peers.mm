/////////////////////////////////////////////////////////////////////////////
// iOS peer factories for controls the minimal wxOSX iPhone port did not
// implement but that Orca (and the now-enabled generic widgets: wxGrid's
// choice editor, wxSearchCtrl, etc.) reference. Each is backed by a plain
// UIView wrapped in the concrete wxWidgetIPhoneImpl (button.mm instantiates
// that class directly, so it is not abstract). This satisfies the linker and
// lays the control out; native appearance/behaviour is a later (step 5) polish.
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/log.h"
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
#include "wx/private/bmpbndl.h"

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

// ---------------------------------------------------------------------------
// Toggle buttons
//
// These were plain UIViews like everything else above, and that is wrong three
// times over. wxWidgetIPhoneImpl::SetBitmap, SetValue and GetValue are all
// empty bodies in the port (src/osx/iphone/window.mm), so the control shows no
// image and always reads false; and a UIView is not a UIControl, so
// InstallEventHandler has nothing to attach UIControlEventTouchUpInside to and
// the control receives no clicks at all.
//
// In OrcaSlicer wxBitmapToggleButton *is* the check box
// (Widgets/CheckBox.cpp, which draws itself entirely by handing the peer a
// check_on/check_off bitmap) and the on/off switch (Widgets/SwitchButton.cpp).
// A blank, dead, always-false control is exactly the "check boxes are missing
// from the left bar" that this port shows.
//
// A UIButton is a UIControl, so touches arrive. The value has to be flipped
// here because wx's own OSX code never does it - on Cocoa the NSButton toggles
// itself and wxToggleButton::OSXHandleClicked only reads the result - and
// OSXHandleClicked runs from the base controlAction below, so the flip has to
// happen before it is called.
class wxIPhoneToggleButtonPeer : public wxWidgetIPhoneImpl
{
public:
    wxIPhoneToggleButtonPeer( wxWindowMac* wxpeer, UIButton* v )
        : wxWidgetIPhoneImpl( wxpeer, v )
    {
    }

    wxInt32 GetValue() const override { return m_value; }

    void SetValue( wxInt32 v ) override { m_value = v; }

    wxBitmap GetBitmap() const override
    {
        return m_bitmap.IsOk() ? m_bitmap.GetBitmap( wxDefaultSize ) : wxBitmap();
    }

    void SetBitmap( const wxBitmapBundle& bitmap ) override
    {
        m_bitmap = bitmap;

        UIButton* b = (UIButton*) GetWXWidget();
        if ( ![b isKindOfClass:[UIButton class]] )
            return;

        // wxAnyButton::DoSetBitmap funnels every bitmap change - the label,
        // the disabled and the hover variants - through the peer's SetBitmap,
        // so this one override keeps all of them in sync.
        [b setImage:(bitmap.IsOk() ? wxOSXGetImageFromBundle( bitmap ) : nil)
           forState:UIControlStateNormal];

        // A UIButton lays its image out inside the content rect, and a
        // UIImageView defaults to UIViewContentModeScaleToFill - so an 18x18
        // check mark is *stretched* to whatever that rect happens to be rather
        // than kept square. On the iPad the checkboxes came out as ~13x6 teal
        // slivers with no visible tick. Keep the aspect ratio and take the
        // insets out of the way, so a wrong content rect can only make the
        // artwork small, never squash it.
        b.imageView.contentMode = UIViewContentModeScaleAspectFit;
        b.contentEdgeInsets     = UIEdgeInsetsZero;
        b.imageEdgeInsets       = UIEdgeInsetsZero;
        b.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
        b.contentVerticalAlignment   = UIControlContentVerticalAlignmentCenter;

        // And report what the geometry actually is, because wx believing the
        // control is 18x18 while it draws 13x6 is the whole question.
        // wxLogMessage, not NSLog: NSLog writes to the device console, which
        // cannot be retrieved from a sideloaded app. wx log messages are routed
        // into Orca's boost sink and land in Documents/log with everything else.
        if ( bitmap.IsOk() ) {
            UIImage* const im = wxOSXGetImageFromBundle( bitmap );
            wxLogMessage( "orca-ios-toggle: button frame %.0fx%.0f, image %.0fx%.0f @%.1fx",
                          (double) b.frame.size.width, (double) b.frame.size.height,
                          (double) im.size.width, (double) im.size.height, (double) im.scale );
        }
    }

    void controlAction( void* sender,
                        wxUint32 controlEvent,
                        WX_UIEvent rawEvent ) override
    {
        if ( controlEvent == UIControlEventTouchUpInside )
            m_value = m_value ? 0 : 1;

        wxWidgetIPhoneImpl::controlAction( sender, controlEvent, rawEvent );
    }

private:
    wxBitmapBundle m_bitmap;
    wxInt32        m_value = 0;
};

static wxWidgetImplType* wxIPhoneMakeToggleButtonPeer( wxWindowMac* wxpeer,
                                                       const wxPoint& pos,
                                                       const wxSize& size,
                                                       const wxBitmapBundle& bitmap )
{
    CGRect r = wxOSXGetFrameForControl( wxpeer, pos, size );

    // -buttonWithType: returns an autoreleased button, hence the retain, which
    // is what button.mm does for wxButton and wxBitmapButton too. Custom
    // rather than RoundedRect: a check box supplies its own artwork and must
    // not get a system button's chrome behind it.
    UIButton* v = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    v.frame = r;

    wxIPhoneToggleButtonPeer* c = new wxIPhoneToggleButtonPeer( wxpeer, v );

    if ( bitmap.IsOk() )
        c->SetBitmap( bitmap );

    return c;
}

wxWidgetImplType* wxWidgetImpl::CreateToggleButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxString& label,
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    wxWidgetImplType* c =
        wxIPhoneMakeToggleButtonPeer( wxpeer, pos, size, wxBitmapBundle() );
    if ( !label.empty() )
        c->SetLabel( label );
    return c;
}

// tglbtn_osx.cpp routes bitmap toggle buttons to this factory rather than
// CreateToggleButton; the cocoa implementation is in cocoa/tglbtn.mm.
wxWidgetImplType* wxWidgetImpl::CreateBitmapToggleButton( wxWindowMac* wxpeer,
                                    wxWindowMac* WXUNUSED(parent),
                                    wxWindowID WXUNUSED(id),
                                    const wxBitmapBundle& label,
                                    const wxPoint& pos,
                                    const wxSize& size,
                                    long WXUNUSED(style),
                                    long WXUNUSED(extraStyle))
{
    return wxIPhoneMakeToggleButtonPeer( wxpeer, pos, size, label );
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
