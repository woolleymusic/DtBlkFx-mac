# DtBlkFx JUCE port: restart here

Last updated 2026-10-14 (presets, universal build, GitHub prep).

## State

Phases 1 to 3 are done and verified offline. Nothing has been tried in a DAW yet.

- **Phase 1 (audio):** done. The original engine runs inside a JUCE AudioProcessor. AU, VST3 and
  Standalone all build. `auval` passes.
- **Phase 2 (editor):** done. The original editor is rebuilt in JUCE from the original PNG skin
  and layout, with both live spectrograms. Snapshots: `docs/editor-idle.png`,
  `docs/editor-running.png`, `docs/editor-running@2x.png`. The editor matches the original
  screenshot in `docs/data/screenshot.png`.
- **Presets:** done. `.fxp` / `.fxb` load and save (`Source/PresetIO.*`): FxCk, FPCh, FxBk and
  FBCh. Saves FPCh / FBCh with fxID 'h526', fxVersion 1200 and the engine's own chunk
  (getChunk(isPreset=true) for a program, getChunk(false) for a bank), as a VST2 host did with
  the original. The menu items are at the bottom of the spectrogram menu; dropping a file on the
  editor loads it. No real DtBlkFx preset files were in the source tree, so `tests/fixtures/`
  holds spec-exact fixtures written by `verify.py --write-fixtures` straight from the VST 2.4
  layout.
- **Universal build:** done. arm64 + x86_64, macOS 11.0 minimum, FFTW from
  `tools/build_fftw_universal.sh`. `auval` passes on both architectures.
- **GitHub prep:** done, not pushed. Repo will be github.com/woolleymusic/DtBlkFx-mac. README is
  written for GitHub. `docs/header.png` is referenced at the top of the README but does not exist
  yet: Charlie will supply it. `tools/package.sh` makes the release zip.

## Build

From the repo root:

```
tools/build_fftw_universal.sh      # once: FFTW 3.3.11 universal into third_party/fftw-universal
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
tools/package.sh                   # release/DtBlkFx-mac-<version>.zip (AU + VST3 + README + LICENSE)
```

You need JUCE 9.0.2 in `~/JUCE/install`. If `third_party/fftw-universal` is missing, CMake falls
back to Homebrew's arm64-only `libfftw3f.a` with a warning, and the build is arm64 only. Either
way FFTW is linked statically: `otool -L` shows only system frameworks and libraries.

`CMAKE_OSX_DEPLOYMENT_TARGET` has to be set before `project()`. It used to be after, which left
it empty and gave every binary a minimum macOS of 26.0; fixed. Every bundle is ad-hoc signed as
a post-build step, so `codesign -v` passes.

Outputs land in `build/DtBlkFx_artefacts/Release/{AU,VST3,Standalone}`.

## Install

AU (no sudo needed). Move the old copy aside first (never copy over a bundle):

```
mv ~/Library/Audio/Plug-Ins/Components/DtBlkFx.component _trash/
cp -R build/DtBlkFx_artefacts/Release/AU/DtBlkFx.component ~/Library/Audio/Plug-Ins/Components/
```

The universal, signed AU is installed there now (2026-10-14). Old copies are in
`_trash/installed-au/`. The VST3 is not installed.

## Test

```
python3 -I tools/verify.py                              # ALL OK expected (sections 1 to 8)
DTBLKFX_HARNESS_ARCH=x86_64 python3 -I tools/verify.py  # same, on the x86_64 slice via Rosetta
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness list        # params, effects, programs
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness ui          # simulated mouse on the editor
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness state       # save / restore round trip
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness preset-load tests/fixtures/fixture_chunk.fxb [--drop]
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness preset-save out.fxp --program 5
build/DtBlkFxHarness_artefacts/Release/DtBlkFxHarness snapshot out.png --program 5 --feed build/verify/sweep.wav
auval -v aufx DtBk Wool
arch -x86_64 auval -v aufx DtBk Wool
```

Results: `docs/verification-2026-10-14.txt` (arm64 only build) and
`docs/verification-2026-10-14-universal.txt` (both slices, presets, arch comparison). In short:

- Dry (Mixbk 100%): output is bit-identical to the input delayed by the Delay setting (residual
  zero, fixed and random block sizes), on both architectures.
- Wet with every slot a 0 dB Filter: nulls against the delayed input at about -137 dB.
- Filter notch 1 to 4 kHz removes about 80 dB in band and leaves 250 Hz within 1 dB. Contrast,
  HarmShift, Shift and Smear all change the spectrum.
- All 44 programs give finite, non-silent output on noise and on a chord at 44.1 kHz, with both
  512-sample and random 1 to 2048-sample host blocks. Same settings run five times give
  identical output.
- Presets: all four fixtures load exactly (max param error 0, names, current program), by file
  and by drop. Saved .fxp / .fxb headers match the spec, reload exactly, and the saved .fxp chunk
  is byte-identical to the fixture's. Wrong plug-in IDs and truncated files are refused.
- arm64 vs x86_64 on the chord: 27 of 44 programs agree to within 1.2e-6 (float rounding). Programs
  that choose pitches or peaks drift apart after a last-bit difference flips a choice; the worst
  is SlowSilly / MixedUp at max 0.14. Checked: the arm64 build alone with fused multiply-add
  turned off differs from the normal arm64 build by the same amount on those programs, so this
  is float sensitivity in the original algorithms, not a porting fault.
- Earlier: an AddressSanitizer + UBSan build of the harness ran the UI, snapshot, state and
  process tests with no reports (not rerun after the preset work).

## Layout

- `Source/engine/`: original engine. Changes from the original are marked "JUCE port" in comments.
- `Source/engine/AudioEffectXShim.h`: stand-in for the VST2 base class.
- `Source/engine/Gui.h`: the callbacks the engine makes to its editor, as an interface.
- `Source/GuiBridge.h`: implements that interface and feeds the spectrograms.
- `Source/gui/`: the editor pieces (skin, popup sliders, global strip, effect rows, spectrograms).
- `Source/original_gui/`: the original VSTGUI sources, for reference only.

## Engine changes (all small, all commented)

- SSE `_mm_getcsr` denormal scope uses the ARM FPCR flush-to-zero bit on Apple Silicon.
- Windows / OSSpinLock locking became a `std::recursive_mutex`. OSAtomic became builtins.
- `std::auto_ptr` became `std::unique_ptr`, plus a few clang template lookup fixes.
- Chunk fields used `long`, which is 8 bytes on macOS. They are now 4-byte ints, so the state
  format is the same as the Windows plug-in.
- FFTW buffers are zeroed when allocated. WarpMix reads one bin past the spectrum. On Windows that
  memory was zero; on macOS it could be stale heap memory, which turned the output into NaN for
  programs 39 and 41 (SimpleMix, AwfulMix). Found by the harness and now fixed.
- Original quirks kept on purpose: `_param_sync_param` is never set up, there is a fade length bug
  in `zeroFillOutput`, and `setChunk` divides by the program count instead of the param count. All
  three are left as they were, because they are how the original behaved.

## Differences from the original

- macOS AU / VST3 / Standalone instead of Windows VST2. Only the stereo build is ported. Mono
  tracks work: the mono signal goes through both sides and the left side comes out.
- Latency reported to the host is 0, same as the original (`setInitialDelay(0)`). The Delay
  parameter is part of the effect, as it was.
- Parameter changes take effect at host block boundaries, as with VST2.
- Host display text for BlkSz and Fx is worked out from the value JUCE asks about. The original
  always showed the current value. For the current value the text is the same.
- A safety net was added: if the output ever goes non-finite, that block is silenced and the
  engine is reset. The original would have kept outputting NaN.
- Editor: text is anti-aliased Arial (the original used GDI). The popup sliders are JUCE desktop
  windows. Mouse wheel steps are scaled to feel like one notch per step. The window can be scaled
  from a corner grip, which also draws over the bottom-right corner of the last row.

## Not done / next

- Test in Ableton: automation, preset recall, the editor popups and menus on screen, the file
  chooser for preset load / save, and drag and drop from Finder. Opening windows was not allowed,
  so the popup sliders, menus and file choosers were only tested with simulated input. Loading by
  drop was tested by calling the editor's drop handler.
- Not tried on a real Intel Mac, only under Rosetta.
- Releases are ad-hoc signed, not notarised (users need `xattr -dr com.apple.quarantine`).
- Windows kit: CMake keeps the MSVC static runtime line. On Windows you would also need an FFTW
  static library.
- pluginval and the VST3 validator have not been run.
- Git history: commit author metadata contains the machine's local hostname email
  (`...@Charless-MacBook-Pro.local`). No file contents in any revision contain personal paths.
  Set a public `git config user.email` before pushing, and decide whether to rewrite the author
  on the existing commits.
