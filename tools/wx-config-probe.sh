#!/bin/bash
# Local wx-config validator: tests wxUSE_* resolution through the FULL chain
# (defs.h -> platform.h -> setup.h -> osx chkconf -> iphone chkconf -> global chkconf)
# WITHOUT the iOS SDK. Milliseconds vs 30-min CI.
cd /home/claude/wxp
mkdir -p /tmp/wxtest/wx && cp include/wx/osx/setup.h /tmp/wxtest/wx/setup.h
cat > /tmp/probe.cpp <<'EOF'
#define __WXOSX__ 1
#define __WXOSX_IPHONE__ 1
#define __WXMAC__ 1
#define __APPLE__ 1
#include "wx/defs.h"
#if wxUSE_LISTCTRL
#pragma message "RESULT LISTCTRL=ON"
#else
#pragma message "RESULT LISTCTRL=OFF"
#endif
#if wxUSE_FILEDLG
#pragma message "RESULT FILEDLG=ON"
#else
#pragma message "RESULT FILEDLG=OFF"
#endif
#if wxUSE_FILECTRL
#pragma message "RESULT FILECTRL=ON"
#else
#pragma message "RESULT FILECTRL=OFF"
#endif
#if wxUSE_STATBOX
#pragma message "RESULT STATBOX=ON"
#else
#pragma message "RESULT STATBOX=OFF"
#endif
EOF
gcc -fsyntax-only -I /tmp/wxtest -I include /tmp/probe.cpp 2>&1 | grep "RESULT"
