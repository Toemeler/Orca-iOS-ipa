var TargetPage=null;

function OnInit()
{
	TranslatePage();

	TargetPage=GetQueryString("target");
	
	// Orca: fallback timeout in case the C++ -> JS signal fails (e.g., WebKit issues).
	// Jump to the target page after 3 minutes so slow computers don't get stuck on a partially loaded page.
	setTimeout("JumpToTarget()",180*1000);
}

function HandleStudio( pVal )
{
	let strCmd=pVal['command'];
	
	if(strCmd=='userguide_profile_load_finish')
	{
		JumpToTarget();
	}
}

// Orca-iOS-ipa: was window.open('../'+TargetPage+'/index.html','_self').
//
// window.open() is the one call on this path that iOS can refuse in silence.
// WKPreferences.javaScriptCanOpenWindowsAutomatically defaults to NO on iOS
// (YES on macOS), and this runs from evaluateJavaScript, which carries no user
// activation - so a blocked call just returns null, with no exception for
// evaluateJavaScript's completion handler to report. The page then sits on
// "Loading......" for ever, which is exactly what an iPad showed while the
// simulator went through to the welcome page.
//
// This is a same-page navigation to a sibling directory, and location.replace
// is the plain way to say that: no popup policy, no user-activation
// requirement, no WKUIDelegate involvement, and it leaves no history entry,
// which matches '_self' on a loading splash better than window.open did.
function JumpToTarget()
{
	window.location.replace('../'+TargetPage+'/index.html');
}
