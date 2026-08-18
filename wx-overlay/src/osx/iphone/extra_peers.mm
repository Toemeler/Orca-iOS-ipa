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
        UIImage* const im = bitmap.IsOk() ? wxOSXGetImageFromBundle( bitmap ) : nil;

        // Not -setImage:forState:. A UIButton draws its image inside a content
        // rect it computes itself, and on iOS 15+ that computation goes through
        // UIButtonConfiguration, whose default insets are far larger than an
        // 18x18 check box - so an 18pt image in an 18pt button was being
        // aspect-fitted down to about 7pt. The measurement that proved it:
        //
        //   orca-ios-toggle: button frame 18x18, image 18x18 @2.0x
        //
        // both correct, and still drawn small. So bypass the button's content
        // rect entirely and own the image view: one subview, pinned to the
        // whole of bounds, autoresizing with it.
        if ( m_imageView == nil ) {
            m_imageView = [[UIImageView alloc] initWithFrame:b.bounds];
            m_imageView.contentMode  = UIViewContentModeScaleAspectFit;
            m_imageView.autoresizingMask =
                UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            m_imageView.userInteractionEnabled = NO;
            [b addSubview:m_imageView];
        }
        m_imageView.frame = b.bounds;
        m_imageView.image = im;

        // A UIButton lays its image out inside the content rect, and a
        // UIImageView defaults to UIViewContentModeScaleToFill - so an 18x18
        // check mark is *stretched* to whatever that rect happens to be rather
        // than kept square. On the iPad the checkboxes came out as ~13x6 teal
        // slivers with no visible tick. Keep the aspect ratio and take the
        // insets out of the way, so a wrong content rect can only make the
        // artwork small, never squash it.
        // And report what the geometry actually is, because wx believing the
        // control is 18x18 while it draws 13x6 is the whole question.
        // wxLogMessage, not NSLog: NSLog writes to the device console, which
        // cannot be retrieved from a sideloaded app. wx log messages are routed
        // into Orca's boost sink and land in Documents/log with everything else.
        if ( im != nil ) {
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
    UIImageView*   m_imageView = nil;
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
//
// AND IT IS ALSO THE CONTROL, now. It was a bare UIView for a long time and
// that had one visible consequence: a wxComboBox drew *nothing at all*. Not a
// box, not the selected string, not an arrow - an empty rectangle the size of
// a control, with no way to open it. Every dialog in this application that has
// one is affected, and several are reachable on a tablet: the storage and
// group pickers in "Send to printer" (PrintHostDialogs.cpp), the filament
// picker in the AMS material settings, the three pickers in the extrusion
// calibration, and both in the SLA import dialog. All of them are
// wxCB_READONLY - a fixed list of choices, which is exactly what a pull-down
// button is for.
//
// A pull-down UIButton and not a UIPickerView, which is what the port's
// wxChoice uses and what the comment here used to promise. A UIPickerView is
// 216 points of spinning wheel *inside the layout*: it would push every dialog
// that has a combo box off the screen, and iOS has not used a wheel for a
// simple list since UIMenu learned to be a pull-down. This is the control the
// system itself uses for the same job, it costs the height of a button, and it
// anchors its list under the thing that was tapped.
class wxIPhoneComboBoxPeer : public wxWidgetIPhoneImpl, public wxComboWidgetImpl
{
public:
    wxIPhoneComboBoxPeer( wxWindowMac* wxpeer, UIButton* v )
        : wxWidgetIPhoneImpl( wxpeer, v ), m_button( v ) {}

    int GetSelectedItem() const override { return m_selection; }

    void SetSelectedItem(int item) override
    {
        m_selection = item;
        RebuildMenu();
    }

    int GetNumberOfItems() const override
        { return static_cast<int>(m_items.GetCount()); }

    void InsertItem(int pos, const wxString& item) override
    {
        m_items.Insert( item, pos );
        if ( m_selection >= pos )
            ++m_selection;
        RebuildMenu();
    }

    void RemoveItem(int pos) override
    {
        m_items.RemoveAt( pos );
        if ( m_selection == pos )
            m_selection = wxNOT_FOUND;
        else if ( m_selection > pos )
            --m_selection;
        RebuildMenu();
    }

    void Clear() override
    {
        m_items.Clear();
        m_selection = wxNOT_FOUND;
        RebuildMenu();
    }

    wxString GetStringAtIndex(int pos) const override
    {
        return ( pos >= 0 && pos < static_cast<int>(m_items.GetCount()) )
                    ? m_items[pos] : wxString();
    }

    int FindString(const wxString& text) const override
        { return m_items.Index( text ); }

    // wxComboBox::Popup(). -performPrimaryAction is what a tap on the button
    // does, and the button's primary action is showing its menu.
    void Popup() override
    {
        [m_button performPrimaryAction];
    }

private:
    // Rebuilt from scratch on every change rather than edited in place: UIMenu
    // and UIAction are immutable, and a combo box in this application is a
    // dozen rows that are populated once.
    void RebuildMenu()
    {
        const size_t count = m_items.GetCount();

        NSMutableArray<UIAction*>* const actions =
            [NSMutableArray arrayWithCapacity:count];

        for ( size_t i = 0; i < count; ++i )
        {
            wxCFStringRef cfTitle( m_items[i] );
            const int index = static_cast<int>(i);

            UIAction* const action =
                [UIAction actionWithTitle:cfTitle.AsNSString()
                                    image:nil
                               identifier:nil
                                  handler:^(__kindof UIAction* a) {
                    (void) a;
                    Choose( index );
                }];
            // The tick iOS puts against the row that is already chosen. Doing
            // it by hand rather than with changesSelectionAsPrimaryAction
            // because wx owns the selection: the menu has to follow it when
            // SetSelection() is called from the application, and not the other
            // way round.
            if ( index == m_selection )
                action.state = UIMenuElementStateOn;

            [actions addObject:action];
        }

        m_button.menu = [UIMenu menuWithTitle:@"" children:actions];
        m_button.showsMenuAsPrimaryAction = YES;

        RefreshTitle();
    }

    // A row was tapped. The selection moves first, so that anything the
    // handler asks the combo box during the event sees the new value.
    void Choose(int index)
    {
        m_selection = index;
        RebuildMenu();

        // wxComboBox::OSXHandleClicked() is what sends wxEVT_COMBOBOX with the
        // index and the string filled in, the same call the Cocoa peer makes.
        // Sent even when the row chosen is the one that was already ticked:
        // the user did press something, and a handler that only cares about
        // changes can compare.
        wxWindowMac* const peer = GetWXPeer();
        if ( peer != nullptr )
            peer->OSXHandleClicked( 0 );
    }

    void RefreshTitle()
    {
        wxString label;
        if ( m_selection >= 0 && m_selection < static_cast<int>(m_items.GetCount()) )
            label = m_items[m_selection];

        wxCFStringRef cfLabel( label );

        // -configuration returns a copy, so it has to be given back for any of
        // this to take effect.
        UIButtonConfiguration* const cfg = m_button.configuration;
        cfg.title = cfLabel.AsNSString();
        m_button.configuration = cfg;
    }

    UIButton*     m_button = nil;   // not owned; wxWidgetIPhoneImpl holds it
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

    // Grey rather than plain: a combo box has to read as a control that can be
    // opened even before it is touched, which on a form is what the filled
    // style says. The chevron pair trailing the title is the system's own mark
    // for a pull-down, and putting the title first keeps it lined up with the
    // text controls a dialog puts either side of it.
    UIButtonConfiguration* const cfg = [UIButtonConfiguration grayButtonConfiguration];
    cfg.image          = [UIImage systemImageNamed:@"chevron.up.chevron.down"];
    cfg.imagePlacement = NSDirectionalRectEdgeTrailing;
    cfg.imagePadding   = 6;
    cfg.titleAlignment = UIButtonConfigurationTitleAlignmentLeading;
    cfg.contentInsets  = NSDirectionalEdgeInsetsMake( 4, 10, 4, 10 );

    // +buttonWithConfiguration: returns an autoreleased button, hence the
    // retain: wxWidgetIPhoneImpl owns its view and releases it.
    UIButton* const v =
        [[UIButton buttonWithConfiguration:cfg primaryAction:nil] retain];
    v.frame = r;
    v.contentHorizontalAlignment = UIControlContentHorizontalAlignmentFill;

    return new wxIPhoneComboBoxPeer( wxpeer, v );
}
