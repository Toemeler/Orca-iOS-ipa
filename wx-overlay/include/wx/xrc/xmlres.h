// Orca-iOS-ipa: minimal wx/xrc/xmlres.h. Orca includes this header widely but
// never calls wxXmlResource/XRCID/XRCCTRL (verified by grep), so a no-op
// declaration satisfies the include without enabling the whole XRC subsystem
// (which would pull wxUSE_XRC + expat XML handlers). NON-XRC builds only.
#ifndef _WX_XRC_XMLRES_STUB_H_
#define _WX_XRC_XMLRES_STUB_H_
#include "wx/defs.h"
#include "wx/string.h"
#endif
