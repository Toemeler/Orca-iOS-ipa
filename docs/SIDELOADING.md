# Sideloading the unsigned IPA

The release IPAs are unsigned. Install options:

- **SideStore / AltStore**: import the IPA; it is re-signed with your free Apple ID
  (7-day refresh, 3-app limit) or a paid developer certificate (1 year).
- **TrollStore** (supported iOS versions only): permanent install, no re-signing.
- **Xcode**: open the Payload .app in a wrapper project and run to your own iPad.

The app requests no special entitlements. File sharing is enabled, so profiles,
projects and exported G-code are visible in the Files app under OrcaSlicer.
