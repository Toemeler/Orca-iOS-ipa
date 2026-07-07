///////////////////////////////////////////////////////////////////////////////
// Name:        src/osx/iphone/dirdlg.mm
// Purpose:     wxDirDialog implementation for wxOSX/iPhone (Orca-iOS-ipa)
// Licence:     wxWindows licence
//
// Bridges wxDirDialog to UIDocumentPickerViewController in folder-selection
// mode (UTTypeFolder). Same synchronous nested-run-loop pattern as the
// iphone wxFileDialog shim in filedlg.mm. Orca only consumes GetPath() after
// ShowModal() == wxID_OK, which the base class serves from m_path.
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DIRDLG

#include "wx/dirdlg.h"
#include "wx/app.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

wxIMPLEMENT_CLASS(wxDirDialog, wxDialog);

@interface WXDirPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) BOOL done;
@property(nonatomic, assign) BOOL cancelled;
@property(nonatomic, strong) NSArray<NSURL *> *urls;
@end

@implementation WXDirPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    self.urls = urls;
    self.done = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    self.cancelled = YES;
    self.done = YES;
}
@end

bool wxDirDialog::Create(wxWindow *parent, const wxString& message,
                         const wxString& defaultPath, long style,
                         const wxPoint& WXUNUSED(pos),
                         const wxSize& WXUNUSED(size),
                         const wxString& WXUNUSED(name))
{
    m_message = message;
    m_path    = defaultPath;
    m_parent  = parent;
    SetWindowStyle(style);
    return true;
}

int wxDirDialog::ShowModal()
{
    @autoreleasepool {
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc]
                initForOpeningContentTypes:@[ UTTypeFolder ]];
        picker.allowsMultipleSelection = HasFlag(wxDD_MULTIPLE);

        WXDirPickerDelegate *del = [[WXDirPickerDelegate alloc] init];
        picker.delegate = del;

        UIWindow *keyWindow = nil;
        for (UIWindow *w in [UIApplication sharedApplication].windows) {
            if (w.isKeyWindow) { keyWindow = w; break; }
        }
        UIViewController *root = keyWindow.rootViewController;
        while (root.presentedViewController) root = root.presentedViewController;
        [root presentViewController:picker animated:YES completion:nil];

        while (!del.done) {
            [[NSRunLoop currentRunLoop]
                runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }

        if (del.cancelled || del.urls.count == 0)
            return wxID_CANCEL;

        m_paths.Clear();
        for (NSURL *url in del.urls) {
            [url startAccessingSecurityScopedResource];
            m_paths.Add(wxString::FromUTF8([url.path UTF8String]));
        }
        m_path = m_paths[0];
        return wxID_OK;
    }
}

#endif // wxUSE_DIRDLG
