# Local validation tools (no CI, no iOS SDK, milliseconds)

## wx-config-probe.sh + wx-sdk-stubs/
Tests wxUSE_* flag resolution through wx's real config chain
(defs.h -> platform.h -> setup.h -> osx/chkconf -> iphone/chkconf -> global chkconf)
using gcc -fsyntax-only with stubbed Apple SDK headers. Lets us verify chkconf
patches BEFORE spending 30-min CI runs. Usage: point -I at wx-sdk-stubs and a wx
include dir with the patches applied; use `#if wxUSE_X` + `#pragma message`.

Caveat: pulls wx/unix/chkconf.h (Apple is unix-family) which errors on
inotify/kqueue absence — harmless for isolated flag probes; add stubs if needed.
