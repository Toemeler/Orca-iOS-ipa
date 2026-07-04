///////////////////////////////////////////////////////////////////////////////
// Name:        src/osx/iphone/filedlg.mm
// Purpose:     wxFileDialog implementation for wxOSX/iPhone (Orca-iOS-ipa)
// Licence:     wxWindows licence
//
// Bridges wxFileDialog to UIDocumentPickerViewController. Orca only consumes the
// result (GetPath/GetPaths/GetFilename/GetDirectory, all provided by the base
// class from m_path/m_fileName/m_dir), so this only implements ShowModal() to
// drive the native picker synchronously via a nested run loop.
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/filedlg.h"
#include "wx/filename.h"
#include "wx/app.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

wxIMPLEMENT_DYNAMIC_CLASS(wxFileDialog, wxFileDialogBase);

// Delegate captures the picked URL(s) and stops the nested run loop.
@interface WXFilePickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) BOOL done;
@property(nonatomic, assign) BOOL cancelled;
@property(nonatomic, strong) NSArray<NSURL *> *urls;
@end

@implementation WXFilePickerDelegate
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

bool wxFileDialog::Create(wxWindow *parent, const wxString& message,
                          const wxString& defaultDir, const wxString& defaultFile,
                          const wxString& wildCard, long style,
                          const wxPoint& pos, const wxSize& size, const wxString& name)
{
    m_message   = message;
    m_dir       = defaultDir;
    m_fileName  = defaultFile;
    m_wildCard  = wildCard;
    m_parent    = parent;
    SetWindowStyle(style);
    return true;
}

int wxFileDialog::ShowModal()
{
    @autoreleasepool {
        const bool saving = (GetWindowStyle() & wxFD_SAVE) != 0;
        const bool multiple = (GetWindowStyle() & wxFD_MULTIPLE) != 0;

        NSArray<UTType *> *types = @[ UTTypeItem ];
        UIDocumentPickerViewController *picker;
        if (saving) {
            // For save, export a temp file the caller will overwrite; iOS has no
            // "type a filename" panel, so we stage the chosen name in tmp first.
            NSString *fname = m_fileName.IsEmpty()
                ? @"untitled"
                : [NSString stringWithUTF8String:m_fileName.utf8_str()];
            NSURL *tmp = [[NSURL fileURLWithPath:NSTemporaryDirectory()]
                            URLByAppendingPathComponent:fname];
            [[NSData data] writeToURL:tmp atomically:YES];
            picker = [[UIDocumentPickerViewController alloc]
                        initForExportingURLs:@[ tmp ] asCopy:YES];
        } else {
            picker = [[UIDocumentPickerViewController alloc]
                        initForOpeningContentTypes:types asCopy:YES];
        }
        picker.allowsMultipleSelection = multiple;

        WXFilePickerDelegate *del = [[WXFilePickerDelegate alloc] init];
        picker.delegate = del;

        UIWindow *keyWindow = nil;
        for (UIWindow *w in [UIApplication sharedApplication].windows) {
            if (w.isKeyWindow) { keyWindow = w; break; }
        }
        UIViewController *root = keyWindow.rootViewController;
        while (root.presentedViewController) root = root.presentedViewController;
        [root presentViewController:picker animated:YES completion:nil];

        // Pump the run loop until the delegate reports completion.
        while (!del.done) {
            [[NSRunLoop currentRunLoop]
                runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }

        if (del.cancelled || del.urls.count == 0)
            return wxID_CANCEL;

        m_pickedPaths.Clear();
        m_pickedNames.Clear();
        for (NSURL *url in del.urls) {
            [url startAccessingSecurityScopedResource];
            wxString p = wxString::FromUTF8([url.path UTF8String]);
            m_pickedPaths.Add(p);
            m_pickedNames.Add(wxFileName(p).GetFullName());
        }
        m_path = m_pickedPaths[0];
        wxFileName fn(m_path);
        m_fileName = fn.GetFullName();
        m_dir = fn.GetPath();
        return wxID_OK;
    }
}
