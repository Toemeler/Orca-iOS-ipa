///////////////////////////////////////////////////////////////////////////////
// Name:        wx/osx/iphone/evtloop.h
// Purpose:     declaration of wxGUIEventLoop for wxOSX/iPhone (Orca-iOS-ipa)
// Licence:     wxWindows licence
// Matches the out-of-line definitions in src/osx/iphone/evtloop.mm exactly.
///////////////////////////////////////////////////////////////////////////////
#ifndef _WX_OSX_IPHONE_EVTLOOP_H_
#define _WX_OSX_IPHONE_EVTLOOP_H_

class WXDLLIMPEXP_BASE wxGUIEventLoop : public wxCFEventLoop
{
public:
    wxGUIEventLoop();
    ~wxGUIEventLoop();

    virtual void WakeUp() override;

protected:
    virtual int DoDispatchTimeout(unsigned long timeout) override;
    virtual void OSXDoRun() override;
    virtual void OSXDoStop() override;
    virtual CFRunLoopRef CFGetCurrentRunLoop() const override;
};

#endif // _WX_OSX_IPHONE_EVTLOOP_H_
