// probe_boot.mm - instrument the wx/UIKit startup handshake from the app side.
//
// Six runs have now ended with the same two lines in the probe log - "static
// init ran", "wxApp ctor ran" - and nothing after them. The chain those two
// lines sit in is known exactly:
//
//   main -> wxEntryReal -> wxApp::CallOnInit()   [iPhone: returns true, no-op]
//                       -> OnRun -> MainLoop
//                       -> wxGUIEventLoop::OSXDoRun()
//                       -> UIApplicationMain(1, "app", @"UIApplication",
//                                            @"wxAppDelegate")
//                       -> -[wxAppDelegate applicationDidFinishLaunching:]
//                       -> wxApp::OSXOnDidFinishLaunching()
//                       -> OnInit()
//
// and src/osx/carbon/app.cpp shows OSXOnDidFinishLaunching does nothing except
// call OnInit, ignoring its result. So exactly one of these is true, and the
// system log cannot tell them apart:
//
//   A. wxAppDelegate is not in the binary. It is named only as a *string* by
//      UIApplicationMain, so nothing forces the linker to keep the object file
//      from a static archive. UIKit would then run with a nil delegate: full
//      scene setup in the log, and no launch callback ever.
//   B. The delegate exists but UIKit never calls the legacy
//      applicationDidFinishLaunching: on this iOS version.
//   C. Step-2 patch 0208 defers OSXOnDidFinishLaunching with
//      dispatch_async(dispatch_get_main_queue()) and that block is never
//      drained.
//   D. It crashes. Nobody has ever checked: sim-launch-verify.sh counted crash
//      reports matching "OrcaSlicer*", so for WxProbe/WxSmoke it printed
//      "crash reports: 0" no matter what happened.
//
// This file settles all four without rebuilding wx. It logs the delegate class
// at +load (A), traces both launch callbacks (B), enqueues its own main-queue
// block behind wx's (C), and installs exception/signal/atexit handlers (D).
//
// If OnInit still has not run a few seconds in, it calls
// OSXOnDidFinishLaunching directly. A UI that appears only after that rescue
// proves the fault is in the handshake and not in any widget.
//
// License: AGPL-3.0.

#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <dispatch/dispatch.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <wx/app.h>

// Same destination as main.cpp's report(): stderr, plus a file under $HOME that
// the harness copies out of the data container. Opened per-call and closed
// again so nothing is lost when the process dies without unwinding.
extern "C" void probe_log(const char* what, const char* how)
{
    std::fprintf(stderr, "WXPROBE %-22s %s\n", what, how);
    std::fflush(stderr);

    const char* home = std::getenv("HOME");
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/Documents/wxprobe.log", home ? home : "/tmp");
    FILE* f = std::fopen(path, "a");
    if (f == nullptr) {
        std::snprintf(path, sizeof(path), "%s/wxprobe.log", home ? home : "/tmp");
        f = std::fopen(path, "a");
    }
    if (f != nullptr) {
        std::fprintf(f, "WXPROBE %-22s %s\n", what, how);
        std::fflush(f);
        std::fclose(f);
    }
}

static bool g_on_init_seen = false;

// main.cpp calls this from OnInit so the watchdog below knows whether the
// normal path already worked.
extern "C" void probe_note_oninit(void) { g_on_init_seen = true; }

// ---------------------------------------------------------------- D: dying

static void probe_signal_handler(int sig)
{
    const char* name = "?";
    switch (sig) {
        case SIGABRT: name = "SIGABRT"; break;
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGTRAP: name = "SIGTRAP"; break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGPIPE: name = "SIGPIPE"; break;
    }
    probe_log("FATAL signal", name);
    _exit(128 + sig);
}

static void probe_uncaught_exception(NSException* e)
{
    probe_log("FATAL ObjC exception",
              [[NSString stringWithFormat:@"%@: %@ | %@",
                    e.name, e.reason, [e.callStackSymbols componentsJoinedByString:@" <- "]]
                  UTF8String]);
}

static void probe_atexit(void)
{
    probe_log("atexit", g_on_init_seen ? "process exiting (OnInit had run)"
                                       : "process exiting WITHOUT OnInit ever running");
}

// ------------------------------------------------- B/C: the launch handshake

static IMP g_orig_did_finish = nullptr;
static IMP g_orig_will_finish = nullptr;

static void probe_did_finish(id self, SEL _cmd, UIApplication* app)
{
    probe_log("delegate", "applicationDidFinishLaunching: ENTERED");

    if (g_orig_did_finish != nullptr)
        ((void (*)(id, SEL, UIApplication*)) g_orig_did_finish)(self, _cmd, app);

    probe_log("delegate", "applicationDidFinishLaunching: returned");

    // Queued *after* whatever patch 0208's dispatch_async enqueued, so if this
    // one runs and OnInit still has not, the main queue is draining and the
    // deferral is not the problem.
    dispatch_async(dispatch_get_main_queue(), ^{
        probe_log("main queue",
                  g_on_init_seen ? "drained - and OnInit had already run"
                                 : "drained - but OnInit still has NOT run");
    });
}

static BOOL probe_will_finish(id self, SEL _cmd, UIApplication* app, NSDictionary* opts)
{
    probe_log("delegate", "application:willFinishLaunchingWithOptions: ENTERED");
    BOOL r = YES;
    if (g_orig_will_finish != nullptr)
        r = ((BOOL (*)(id, SEL, UIApplication*, NSDictionary*)) g_orig_will_finish)(
                self, _cmd, app, opts);
    probe_log("delegate", r ? "willFinishLaunching returned YES"
                            : "willFinishLaunching returned NO");
    return r;
}

// ------------------------------------------------------------------ install

@interface ProbeBoot : NSObject
@end

@implementation ProbeBoot

+ (void)load
{
    probe_log("image", "loaded (+load ran)");

    std::atexit(probe_atexit);
    NSSetUncaughtExceptionHandler(&probe_uncaught_exception);
    const int sigs[] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE};
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); ++i) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = probe_signal_handler;
        sigaction(sigs[i], &sa, nullptr);
    }

    // (A) Is wx's delegate class actually in this binary? If this says
    // "MISSING", UIApplicationMain is naming a class that does not exist, the
    // app runs with no delegate, and no launch callback can ever arrive.
    Class d = objc_getClass("wxAppDelegate");
    probe_log("wxAppDelegate class", d != nil ? "present in the binary"
                                              : "MISSING - not linked in!");

    if (d != nil) {
        Method m = class_getInstanceMethod(d, @selector(applicationDidFinishLaunching:));
        if (m != nullptr) {
            g_orig_did_finish = method_getImplementation(m);
            method_setImplementation(m, (IMP) probe_did_finish);
            probe_log("hook", "applicationDidFinishLaunching: installed");
        } else {
            probe_log("hook", "wxAppDelegate has NO applicationDidFinishLaunching:");
        }

        Method w = class_getInstanceMethod(
            d, @selector(application:willFinishLaunchingWithOptions:));
        if (w != nullptr) {
            g_orig_will_finish = method_getImplementation(w);
            method_setImplementation(w, (IMP) probe_will_finish);
            probe_log("hook", "willFinishLaunchingWithOptions: installed");
        }
    }

    // Watchdog + rescue. Runs on the main queue, so reaching it at all is proof
    // the queue drains.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        UIApplication* app = [UIApplication sharedApplication];
        probe_log("watchdog t=4s",
                  [[NSString stringWithFormat:
                        @"OnInit_seen=%d sharedApplication=%@ delegate=%@ windows=%lu",
                        (int) g_on_init_seen, app ? @"yes" : @"nil",
                        app.delegate ? NSStringFromClass([app.delegate class]) : @"(nil)",
                        (unsigned long)(app ? app.windows.count : 0)] UTF8String]);

        if (!g_on_init_seen) {
            probe_log("rescue", "OnInit never ran - calling OSXOnDidFinishLaunching directly");
            if (wxTheApp != nullptr) {
                wxTheApp->OSXOnDidFinishLaunching();
                probe_log("rescue", g_on_init_seen ? "OnInit ran under the rescue"
                                                   : "rescue returned, OnInit still not seen");
            } else {
                probe_log("rescue", "wxTheApp is NULL");
            }
        }
    });
}

@end
