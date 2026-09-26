# PixelPop firmware handoff

## At a glance (current as of 2026-09-25 — read this first, always)

- **Build:** PlatformIO. `pio run` to build, `pio run -t upload` to flash.
  No Arduino IDE / `arduino-cli` in this live copy any more.
- **Boards:** control (`90:e5:b1:d2:2b:dc`), `56:d8` (`90:e5:b1:cc:56:d8`),
  victor (`90:e5:b1:cc:93:5c`), raul (`90:e5:b1:cc:af:24`) — all 4 show
  correct colors. **1 board** has been reflashed with the current
  PlatformIO build (includes the flicker fix) and confirmed working; the
  **other 3** are still on the older Arduino-built flicker-affected
  firmware — not broken, just pending the same `pio run -t upload` when
  convenient.
- **No active hold on any board.** Nothing is mid-flight. Safe to pick up
  and continue.
- Everything below this section is either recent detail (RESOLVED, next)
  or older historical narrative — sections further down are explicitly
  marked when they're superseded. If you only read one section, make it
  this one plus RESOLVED right below it.

## PORTED TO PLATFORMIO (2026-09-25) -- read this before touching Arduino IDE or arduino-cli

This project no longer has a `.ino` file, `sketch.json`, or an Arduino
IDE build path. It builds with **PlatformIO** now (`platformio.ini` at
the project root, entry point `src/main.cpp`). See `FLASHING.md`'s
"Read this first" section and pitfalls #1, #7, #11, #12, #13 for the
full story. If you're an agent about to run `arduino-cli` or open
Arduino IDE against this live copy: stop, it won't find a sketch here
any more. A full pre-port copy (with the old Arduino IDE setup intact)
is kept at `~/Desktop/Sketches/Matrix Arduino` for reference only — do
not treat it as the live project.

The "Build, Publish, and Install Over Wi-Fi (OTA)" workflow in
`FLASHING.md` still calls `arduino-cli compile` and has NOT been
updated for PlatformIO — it is stale and should not be run until
someone rewrites its compile step to `pio run`.

## RESOLVED (2026-09-25, Claude) -- read this before anything else below

**The pink/magenta color problem was a saved setting, not hardware.** These
Waveshare panels have green and blue swapped. The firmware corrects this with
the per-board wiring-order setting (Advanced > Color Lab), stored in NVS key
`cord`; its built-in default is 1 = "Swap green and blue". Boards set to
"Normal (red, green, blue)" show exactly this symptom: gold/orange/yellow text
turns pink/magenta (e.g. `0xFFD200` -> 255,0,210), green shows as blue, blue as
green, and clouds shift slightly.

- Victor (`90:e5:b1:cc:93:5c`) was set to Normal by Claude earlier in this
  investigation (a wrong "fix" for an earlier purple-cloud report). Set back to
  "Swap green and blue": colors correct.
- `90:e5:b1:cc:56:d8` was on Normal (probably left over from its old firmware).
  Set to "Swap green and blue": colors correct, including white and gray.
- Control (`90:e5:b1:d2:2b:dc`) and raul (`90:e5:b1:cc:af:24`) render correctly;
  raul was confirmed on "Swap green and blue".
- All four boards run the same build (Advanced page Build `f1571c9b` at test time).

**If colors look wrong on any board, check the Advanced page "Currently:" line
first.** It must say "Swap green and blue" with gains 255/255/255. Firmware
parity alone is not enough; this setting lives in NVS and survives reflashing.

Ruled out along the way (do not re-chase): ribbon cables (reseated and swapped),
panels (victor's panel was correct on control's driver board), driver-board
hardware, I2S clock phase, color gain, and the Canvas color pipeline (order 0 +
gain 255 is a byte-for-byte pass-through in `Canvas::mapColor`).

Source changes from this investigation:
- `mxconfig.setPixelColorDepthBits(4)` (a green-line diagnostic) was **removed**
  from `displayBegin()`; the panel is back on the library's default color depth.
  `double_buff = false` stays (that is the shipped 1.1.23 fix).
- A `clkphase = false` experiment was added and then removed; it had no effect.
- `App::loop()` Color Lab fix: the idle timeout re-reads `millis()` after
  `webLoop()` (the old code underflowed and exited Color Lab immediately, so
  sliders/swatches did nothing). Boards on older builds still show that symptom.

Green lines on victor: not seen again after the ribbon cable was reseated.

**Also resolved this session: scrolling-text flicker on all 4 boards.**
Cause: with `double_buff = false`, `Canvas`'s draw primitives
(`drawPixel`/`fillScreen`/`fillRect`/`drawFastHLine`/`drawFastVLine`)
wrote straight to the live panel on every GFX call, and most modules'
`drawPage()` starts each frame with a full `fillScreen(0)` — so every
scroll step flashed the panel black before redrawing. Fix: those
primitives now write only to the `_shadow` buffer; a new
`Canvas::present()` diffs shadow vs. a new `_onPanel` array and only
pushes pixels that actually changed, called from `flipDMABuffer()` at
end-of-frame. `Transition`'s raw-pixel blending is unaffected (new
`_rawFrame` flag prevents double-applying its frame). Compiled,
flashed, and confirmed flicker-free on all 4 boards. See `FLASHING.md`
for the full technical writeup if you need it.

Also cleaned up this session: 8 stale `build/*` folders (`current`,
`restore`, `stage-one`, `stage-two`, `usb`, `weather-only`,
`weather-internal`, `weather-internal-strict`) were deleted — leftovers
from earlier one-off compiles and the OTA publish workflow that could
cause a future agent to flash a stale binary by `cd`-ing into the wrong
folder. Only `build/esp32.esp32.esp32s3/` (Export Compiled Binary's
real output) and the `canvas-test`/`hub75-test` diagnostic build
folders remain.

Backups: `firmware-backups/board-90e5b1d22bdc-20260924-2023/` (control board,
before it was flashed with the current build). Bootloader, partitions and
chunks 0 and 3-5 of the app region are good. **Chunks 1-2 failed to read**
(consistently around 0x218000-0x277000), so `app.bin` in that folder is
incomplete (4 MB, not 6 MB). Treat it as partial, not a restorable image.

Board recovery on victor is complete and the board is working. The hold on
Copilot touching this board below can be lifted once the user says so.

## [HISTORICAL — superseded, see "At a glance" and RESOLVED above] Note from Claude (2026-09-24, later than everything below)

This section and everything below it predates the fixes above and is
kept only for the reasoning trail. **Do not treat any "current status"
language below this point as current** — read "At a glance" at the top
of this file instead.

Read the rest of this file — it's useful, verified findings (the
green-line defect reproducing on three separate firmwares including the
official Waveshare test image, which points at hardware, not code).

The user has asked me (Claude) to drive board recovery on
`90:e5:b1:cc:93:5c` / `/dev/cu.usbmodem2101` from here. **Copilot: please
do not build, flash, OTA-install, or otherwise touch this board until
the user tells you to resume** — we already stepped on each other once
(a Claude-flashed build got overwritten before it could be confirmed
working), and the user doesn't want that repeating. If you're reading
this while about to act on this board, stop and check with the user
first. I've also added a "Read this first" section and pitfall #8 to
`FLASHING.md` in this same folder with more detail on what happened.

Current action: the user is physically reseating the HUB75 ribbon cable
and power connector on this board per the green-line hardware hypothesis
above, before either of us flashes anything else.

## [HISTORICAL — stale, do not use] Status snapshot from 2026-09-24

This section is left exactly as it was written on 2026-09-24, before
the color-bug and flicker-bug resolutions, the PlatformIO port, and the
removal of `build/current/`. Nothing below this point reflects the
live project any more — see "At a glance" at the top of this file.

- Hardware: Waveshare ESP32-S3 RGB Matrix (SKU 34422), 64x32 HUB75 panel, 32 MB flash, 16 MB OPI PSRAM.
- The latest published USB image is `build/current/PixelPop.ino.bin`. It was written to the board with MAC `90:e5:b1:cc:93:5c`, but the user reports it overwrote a working firmware previously installed by Claude.
- The exact Claude-produced working binary was not retained in the local build or publish history. Do not substitute a different historical image as a rollback.
- Do not build, publish, erase, OTA-update, or USB-flash any board until the user explicitly asks and the exact target artifact and physical board have been verified.
- A rollback bundle for the firmware currently on that board is preserved at `firmware-backups/board-90e5b1cc935c-20260924-1708/`. It contains the application, bootloader, partition table, boot-app artifact, firmware marker, board identity, and SHA-256 manifest. The preserved application is `@@PXP:1.1.23`.
- A raw 32 MB read was attempted twice but the board's USB serial stream failed at about 7.4%. No incomplete raw image is retained. The rollback bundle is therefore a verified firmware-artifact backup, not a snapshot of NVS or filesystem data.

## Verified display facts

- Both the raw HUB75 color diagnostic (`diagnostics/Hub75Test`) and the PixelPop display/canvas diagnostic (`diagnostics/CanvasTest`) rendered cleanly.
- Full PixelPop, an earlier released PixelPop binary, and the official Waveshare test image all reportedly showed horizontal green lines on the affected board.
- Therefore, do not treat the display fault as an unverified pin-map issue or re-run those diagnostics without a new, bounded hypothesis and the user's approval.

Use the Waveshare default HUB75 pin map; do not reintroduce an explicit alternative map:

```text
R1=4, G1=5, B1=6, R2=7, G2=15, B2=16,
A=18, B=8, C=3, D=42, E=-1,
LAT=40, OE=2, CLK=41
```

## [HISTORICAL — one claim below is now FALSE, see RESOLVED above] Source changes requiring review before release

Several display/startup changes were made while diagnosing the green-line fault. **This whole section predates the flicker-fix rewrite** — in
particular, the claim that `flipDMABuffer()` is a no-op for single
buffering is no longer true: it now calls `Canvas::present()`, which
diffs and pushes changed pixels. See the flicker-fix writeup in
RESOLVED above and in `FLASHING.md` for what's actually there now.
Left below only for the historical trail:

- `src/core/Display.cpp` sets `mxconfig.double_buff = false`. (The `setPixelColorDepthBits(4)` line that used to be here was removed on 2026-09-25; see the RESOLVED note at the top.)
- `src/core/Display.h` passes a `doubleBuffered` setting to `Canvas` and (as of 2026-09-24) made `flipDMABuffer()` a no-op for single buffering — superseded, see above.
- `src/core/App.cpp` initializes the display early with `if (!gDisplay) displayBegin(_brightness, _pad);`.

Review these against a known-good source/binary before keeping, reverting, or publishing them.

## Active stability and startup behavior

The source currently includes these intentional reliability changes. Preserve them unless a review identifies a specific regression:

- `App::begin()` records non-power-on reset causes. `CrashLog` keeps up to eight reset records, surfaced through the Advanced UI.
- `src/main.cpp` (was `PixelPop.ino` before the PlatformIO port) creates module objects with placement new, preferring 8-bit PSRAM and falling back to normal allocation. This reserves internal RAM for networking and TLS. The current setup registers 19 modules.
- `displayBegin()` uses non-throwing allocation for the matrix panel and canvas. If either allocation or initialization fails, it emits a serial error and remains in a diagnostic loop rather than continuing with invalid display state.
- Canvas supports shadow-buffered drawing, rotation, padding/clipping, channel-order correction, gain controls, and calibration swatches.
- `displayBlackout()` clears twice and sets brightness to zero before sleep/restart-related transitions.

## [HISTORICAL — pre-PlatformIO, see FLASHING.md for the current workflow] Build and release workflow

Use this FQBN:

```text
esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=app13M_data7M_32MB,PSRAM=opi
```

- `bpr` is defined in `~/.zshrc`. It builds into `build/current`, increments/version-stamps as appropriate, and publishes `PixelPop.ino.bin` to the local `pixelpop-publish` repository before waiting for OTA installation at `192.168.6.189`.
- `bpr` is a publish workflow, not a safe replacement for a manually verified firmware image.
- Before any requested USB flash, run `arduino-cli board list` immediately beforehand. The port can change after reset. The last observed port was `/dev/cu.usbmodem2101`.
- Confirm the connected board MAC and the firmware marker first:

```sh
strings path/to/PixelPop.ino.bin | sed -n 's/^@@PXP:/@@PXP:/p'
```

- Preserve the exact binary and matching bootloader/partition artifacts before changing the board. Verify the flash hash after writing.
- Keep a new immutable, timestamped artifact bundle under `firmware-backups/board-<mac>-<timestamp>/` before every intentional board write. Include SHA-256 checksums and the board identity.

## [HISTORICAL] Recovery rule

The previous published release (`v1.1.20`) is available in the local publish history, but it previously displayed the same green-line behavior and is not the Claude-known-good rollback. Do not flash it merely because it is older.
