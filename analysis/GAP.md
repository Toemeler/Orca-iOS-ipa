# wxWidgets gap analysis (generated from source, 2026-07-02)

Inputs:
- `wx-symbols-used-by-orca.txt` — 505 distinct `wx*` CamelCase symbols grepped from
  `src/slic3r/` at Orca pin `395e070a`.
- `wx-iphone-native-files.txt` — 24 native `.mm` units in wxWidgets `src/osx/iphone/`.

## Buckets

1. **Non-widget symbols (majority of the 505).** Strings, arrays, DC/graphics, events,
   sizers, bitmaps, fonts, timers, streams, base64, regex, config, translations. All live in
   wxBase + wxCore common/osx-core code → compile for iOS unchanged.
2. **Native-backed widgets already in the iPhone port.** wxWindow, top-levels, event loop,
   wxGLCanvas (EAGL), wxTextCtrl, wxButton/wxBitmapButton (anybutton), wxCheckBox, wxChoice,
   wxListBox, wxNotebook, wxToolBar, wxSlider, wxGauge, wxStaticText/Bitmap, wxMenu/Item,
   wxMessageDialog, wxScrollBar.
3. **Generic (self-drawn) fallbacks that work atop bucket 2.** wxAuiManager/wxAuiToolBar,
   wxDataViewCtrl, wxGrid-likes, wxSplitterWindow, wxScrolledWindow, wxPopupWindow,
   wxComboCtrl/popup, wxCollapsiblePane, wxCalendar, wxRichToolTip, wxSearchCtrl (generic),
   wxSpinCtrl (generic), wxTreeCtrl (generic), wxInfoBar (generic), wxWizard, wxBusyInfo.
   Plus **all of Orca's `GUI/Widgets/` custom controls** (they are wxWindow + wxDC drawing).
4. **Must implement (the real work list).**
   - `wxWebView` UIKit/WKWebView backend (adapt the macOS WebKit backend — same WKWebView API).
   - `wxClipboard` (UIPasteboard), `wxFileDialog`/`wxDirDialog` (UIDocumentPicker),
     `wxColourDialog` (UIColorPickerViewController), `wxFontDialog` (generic fallback),
     `wxDragDrop` (optional; stub first).
   - `wxMediaCtrl` shim → AVPlayerLayer (printer camera).
   - `wxStandardPaths`/sandbox path mapping, `wxSingleInstanceChecker` no-op.
5. **Stub-first, restore-later.** wxTaskBarIcon/notifications (→ UNUserNotificationCenter
   later), wxPrintDialog (Orca barely uses OS printing), display-DPI multi-monitor bits.

## Conclusion

The port is not "implement 505 symbols"; it is: keep buckets 1–3 compiling, implement the
~8 items in bucket 4, stub bucket 5. Link Orca with `SLIC3R_OPENGL_ES=1` so the GLES render
path replaces desktop GL/GLEW. Step-3 CI turns the remaining link/runtime errors into an
exact, finite TODO list.
