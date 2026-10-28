# Local OpenSolo 5 controller branding

The development firmware uses OpenSolo 5 branding. Its upstream source lineage
remains the OpenSolo 4 Artoo component recorded in `../UPSTREAM.json`; this is
an unreleased local development build, not a claim of an upstream OpenSolo 5
release or completed hardware qualification.

- `images/logo_startup/Solo_Logo.png` reads **OPENSOLO 5** and retains the existing
  black/white/green style and **212 × 48 pixel** canvas. The final image uses only
  colors already present in the original logo's RGB565 palette, preserving the
  existing shared firmware palette limit.
- `assets.cfg` still maps this file to `Icon_Solo_Startup_Logo`. Both the startup
  and shutdown screens use that symbol.
- `../VERSION` is `5.0.0-dev`; `git_describe_tag` appends the source revision and
  dirty status to the version drawn on the startup screen.
- Active legacy `Contact 3DR Support` messages in `ui_alert.cpp` and
  `ui_bottombox.cpp` now read `OpenSolo 5 support`. Aircraft names, SoloLink
  network names, telemetry, warnings and update-state messages retain their
  existing meanings.

The bitmap was edited with the built-in image tool, then cropped and resampled
for the original LCD asset size and mapped to the original palette. Source
artwork and processing evidence are retained locally under
`/home/jresp/solo/.integration/artoo-builds/opensolo5-branding` and the workstation's
generated-images directory. The committed PNG is the firmware build input.

These changes are local only. They do not replace the accepted Yocto MCU
payload, enable automatic updates, or flash the controller.
