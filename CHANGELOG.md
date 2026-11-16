# Changelog

## 1.2.2 (2026-11-16)

- Fixed: a value "running away" while dragging it (seen in Live 12 with 1.2.0). The popup
  slider only picked Fine or Coarse from vertical movement, so a sideways drag did nothing until
  the hand wobbled a pixel or two. Then the value leapt by the whole sideways distance, and the
  bar (often Fine, picked by a 1 px wobble up) was dragged along after the pointer, out of the
  plug-in window. Now the first movement in any direction picks a bar (up = Fine, sideways or
  down = Coarse) and the bars are placed again at the pointer, so the value never jumps and
  follows the pointer from the first pixel.
- Fixed: trackpad and Magic Mouse momentum scrolling kept moving a value after you let go.
  Momentum events are ignored, and so is the wheel while a popup slider is being dragged.
- Includes 1.2.1 (never released on its own): the Fine / Coarse bars are drawn inside the
  plug-in window instead of as separate screen windows.
- Build: extended attributes are cleared before ad-hoc signing, so building inside an iCloud
  synced folder no longer fails with "resource fork, Finder information, or similar detritus not
  allowed". The release zip no longer carries `._` AppleDouble files.
- Tests: `DtBlkFxHarness drag` (verify.py section 11).

## 1.2.1 (2026-10-15, not published)

- The Fine / Coarse popup sliders are one child component of the editor, kept inside it, instead
  of two desktop windows that could land outside the plug-in and move out of step.

## 1.2.0 (2026-10-14)

- First release: native Apple Silicon + Intel AU and VST3 port of DtBlkFx in JUCE.
