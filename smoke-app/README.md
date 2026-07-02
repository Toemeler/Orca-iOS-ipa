# wx GLES smoke app (stage 2)

Minimal wxWidgets app: one wxFrame + wxGLCanvas rendering a GLES3 triangle,
plus a wxTextCtrl and wxButton to exercise native controls. `build.sh <wx-prefix>
<sdk> <ios-min>` compiles it against the stage-2 wx build and produces WxSmoke.app.
Added when stage 2 first runs; kept deliberately tiny so failures indict wx, not us.
