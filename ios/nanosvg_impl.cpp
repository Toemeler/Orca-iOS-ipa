// Orca-iOS-ipa step-1: provides the NanoSVG implementation for GUI-less
// builds. In desktop builds this macro is defined by a GUI translation unit
// (BitmapCache), which does not exist when SLIC3R_GUI=0.
// License: AGPL-3.0.
#include <cstdio>
#include <cstring>
#include <cmath>
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
