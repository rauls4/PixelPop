# Flashing a New PixelPop Board

This file is the verified procedure for bringing up a new physical
ESP32-S3 PixelPop board (Waveshare ESP32-S3-RGB-Matrix, 64x32 HUB75 panel).
Everything below was confirmed by hand during a real board bring-up
(bootloader/partition offsets read from the toolchain's own generated
files, not copied from a doc). If you are an agent picking this up cold,
read the whole "Known pitfalls" section before touching anything — it
will save you hours.

**This project builds with PlatformIO** (ported 2026-09-25 from Arduino
IDE — see the dated note below). There is no `.ino` file or Arduino IDE
project here any more; the entry point is `src/main.cpp`. A full
pre-port copy of this project, including the Arduino IDE setup, is kept
at `~/Desktop/Sketches/Matrix Arduino` as a fallback/reference — the
live copy this file describes does not need it for normal work.

## Read this first if you are Copilot (or any other agent) — current status

**Most recent incident (2026-09-24):** something ran the OTA "Build,
Publish, and Install" workflow near the bottom of this file
unattended — it compiled the sketch, published the `.bin` to the GitHub
repo, and auto-installed it onto the live board (Board 1) with nobody
watching. That build had never been flashed and tested over USB first.
The board came up frozen showing pixel-artifact garbage. See pitfall #8
below before ever running that workflow again — short version: never
run it unattended, and never with a build that hasn't already been
proven over USB.

**Fix applied:** a plain manual USB recovery flash, per "Existing board
recovery" below — Export Compiled Binary, then esptool the four images
from `build/esp32.esp32.esp32s3/` at the standard offsets (step 3). No
source changes were needed; the source itself (the "Color Lab" live
color-calibration feature — see `Display.h/.cpp`, `App.h/.cpp`,
`Advanced.cpp`) was already verified correct, it just had never been
tested on hardware before that bad unattended OTA push. The user was
given the reflash command and is confirming the result now — if you're
reading this and don't know the outcome, ask before assuming it's fixed
or still broken.

**In-progress feature (source done and committed to the Mac; NOT yet
Exported, flashed, or tested on hardware as of this note):** a
Home-page "firmware update available" banner — checks the same fixed
official GitHub repo as the existing Advanced-page automatic updates
toggle, but only *offers* to install (needs an explicit click), never
installs by itself. This is the safer, human-in-the-loop counterpart to
automatic updates, and it exists specifically because of the incident
above — do not build anything here that installs without a person
confirming first.

Done: `src/core/FwUpdate.h`/`.cpp` have `fwCheckOfficial()` /
`fwStartOfficial()` (thin wrappers around the existing `fwCheck`/
`fwStart`/`AUTO_URL`); `src/core/WebUI.cpp`'s `handleHome()` has a
hidden `id='upd'` card, a matching `UPDATE_JS` PROGMEM script (mirrors
`Advanced.cpp`'s `ADV_JS` `poll()` pattern, reuses the already-public
`/advanced/fstatus` for progress), and `webBegin()` registers
`/update/check` (GET) and `/update/install` (POST). All committed and
md5-verified on the Mac.

Still needed before this is real: **Sketch > Export Compiled Binary**
(not Verify — see pitfall #1), then a manual USB flash per steps 1-3
above, then actually clicking the new button on a real board and
watching it. Given the incident above, do NOT let this get to a board
via the OTA path first — prove it over USB, on a board someone is
watching, before it ever touches the auto-update GitHub repo. Update or
delete this note once that's done.

**Resolved this session (2026-09-25): the pink/magenta title-text
"color bug" was never hardware.** It was a per-board NVS wiring-order
setting (`cord`, Advanced > Color Lab). See pitfall #9 below — read it
before touching cables, panels, clock phase, or power on any board
again over a color complaint. All 4 boards (control `90:e5:b1:d2:2b:dc`,
`56:d8` `90:e5:b1:cc:56:d8`, victor `90:e5:b1:cc:93:5c`, raul
`90:e5:b1:cc:af:24`) are confirmed correct as of this note.

**Resolved this session (2026-09-25): scrolling-text flicker.** With
`mxconfig.double_buff = false` (intentional, keep it), the old
`Canvas` draw primitives (`drawPixel`/`fillScreen`/`fillRect`/
`drawFastHLine`/`drawFastVLine`) wrote straight to the live panel on
*every* Adafruit_GFX call, not just at end-of-frame. Since most
modules' `drawPage()` starts with a full `fillScreen(0)` every
animation frame, this flashed the panel to black on every scroll
step. Fixed: those five primitives now only touch the `_shadow`
buffer when single-buffered; a new `Canvas::present()` diffs the
shadow buffer against a new `_onPanel` tracking array and only calls
`_panel->drawPixel()` for pixels that actually changed;
`flipDMABuffer()` calls `present()` at end-of-frame instead of nothing
happening live. The `Transition` system's raw-pixel blending
(`rawPixel()`, `beginCapture()/endCapture()`, `shadow()`) is
unmodified in behavior — a `_rawFrame` flag tells `flipDMABuffer()`
not to double-apply a frame a transition already drew. `App::loop()`
also got two `gDisplay->present();` safety-net calls after
`drawPage()`. Compiled, flashed, and confirmed stable (no flicker) on
all 4 boards (control, `56:d8`, victor, raul).

**Ported to PlatformIO this session (2026-09-25).** Motivation: the
Arduino IDE workflow had three standing, documented pain points —
pitfall #1 (Upload button computes wrong offsets), pitfall #6
(Terminal.app can't be typed into by computer-use tools), and pitfall #7
(IDE tab clicks unreliable via accessibility automation). PlatformIO's
CLI (`pio run`, `pio run -t upload`) has none of these: no GUI to
misclick, and its upload path correctly derives offsets from
`partitions.csv` (confirmed — see step 3 below). `src/core` and
`src/modules` did not move; PlatformIO's default `src_dir` already
matched this project's existing layout. Only the entry file changed
(`PixelPop.ino` → `src/main.cpp`, includes updated to drop the leading
`src/`) and `platformio.ini` was added (mirrors the old FQBN: 32MB
flash, OPI PSRAM, CDC-on-boot, this project's own `partitions.csv`).
Compiled output was confirmed equivalent (3.31MB vs. the Arduino
build's 3.22MB — the small difference is expected from library/core
version differences, not a red flag) and flashed/tested successfully on
one board (steady scrolling, transitions, live Color Lab swatches,
spinner, Wi-Fi, settings site — all confirmed working). Two gotchas hit
during the port are now pitfalls #12 and #13 below — read those before
re-deriving them. The old Arduino IDE files (`PixelPop.ino`,
`sketch.json`, the `build/esp32.esp32.esp32s3/` output folder) were
deleted from this live copy as part of the port; they still exist in
the `Matrix Arduino` backup if ever needed.

## 0. Board settings (now in `platformio.ini` — nothing to set per-machine)

All of this now lives in `platformio.ini` at the project root — you
don't need to configure anything in an IDE. For reference, this is what
it encodes (equivalent to the old Arduino IDE FQBN
`esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=app13M_data7M_32MB,PSRAM=opi`):

- Board: ESP32S3, `board_build.arduino.memory_type = qio_opi` (quad flash
  + octal/OPI PSRAM)
- USB CDC on boot: `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`
- Flash size: `board_upload.flash_size = 32MB`
- Partitions: `board_build.partitions = partitions.csv` (this project's
  own file, not a stock scheme)

The project's own `partitions.csv` (next to `platformio.ini`) is what
actually determines the partition table on the chip:

```
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x10000,
otadata,   data, ota,     0x19000,  0x2000,
app0,      app,  ota_0,   0x20000,  0x5F0000,
app1,      app,  ota_1,   0x610000, 0x600000,
spiffs,    data, spiffs,  0xC10000, 0x400000,
```

## 1. Find the USB port

```bash
ls /dev/cu.usbmodem*
```

## 2. Build

```bash
cd "/Users/raul/Desktop/Sketches/Matrix/PixelPop"
pio run
```

First run downloads the `espressif32` platform/toolchain and the
pinned libraries — takes a few minutes; after that it's fast (cached).
Output lands in `.pio/build/pixelpop/` (`bootloader.bin`, `firmware.bin`,
`partitions.bin`, `firmware.elf`). No separate `boot_app0.bin` in that
folder — PlatformIO supplies it itself at upload time from its own
framework package, you don't need to track it.

## 3. Flash

```bash
pio run -t upload
```

PlatformIO auto-detects the port; pass `--upload-port /dev/cu.usbmodemXXXX`
explicitly if more than one board is connected at once. This correctly
derives all four offsets (0x0 / 0x8000 / 0x19000 / 0x20000) from
`partitions.csv` on its own — confirmed by watching a real upload log,
no manual esptool invocation needed any more (unlike the old Arduino IDE
workflow — see pitfall #1, now historical).

If you get `A fatal error occurred: Failed to connect to ESP32-S3: No
serial data received` — see pitfall #13 below (native-USB auto-reset
quirk, not a real connection problem).

Close the Serial Monitor / any open `pio device monitor` session first
if one is running — it holds the port open and esptool will fail with
"Resource busy".

## 4. First-time Wi-Fi setup

After a clean flash, the board opens its own Wi-Fi network named
`PixelPop`. Join it, then browse to `http://192.168.4.1` (it usually
pops up automatically) and enter your home Wi-Fi credentials.

---

## Known pitfalls (read this before debugging a "dead" board)

### 1. [HISTORICAL — Arduino IDE only, retired 2026-09-25] Arduino-ESP32 core 3.3.11's Upload button uses the WRONG offsets — confirmed bug

This project no longer uses the Arduino IDE (see the top of this file),
so this pitfall shouldn't recur here — kept for history and in case
you're working from the `Matrix Arduino` backup copy instead.

**Symptom:** boot loop, serial shows:
```
E (31) boot: ota data partition invalid and no factory, will try all partitions
E (32) esp_image: image at 0x20000 has invalid magic byte
E (34) boot: OTA app partition slot 0 is not bootable
```

**Cause:** the IDE's/`arduino-cli upload`'s own `flash_args` generation
uses stale default offsets (bootloader 0x0, partitions 0x8000, otadata
0xe000, app 0x10000) regardless of the project's real `partitions.csv`
(confirmed by reading `build/esp32.esp32.esp32s3/flash_args` directly).
The partition **table itself** (baked into `partitions.bin`) IS correct
(app0 at 0x20000) — only the upload tool's offsets are wrong. This
happens whether Partition Scheme is set to the named scheme or
"Custom", and whether or not "Erase All Flash Before Sketch Upload" is
on.

**Fix:** never use the Upload button or `arduino-cli upload`. Always
`Sketch > Export Compiled Binary`, then flash manually with esptool
using the offsets from step 3 above.

### 2. USB power alone can be too weak for the panel

**Symptom:** garbled/partial display — stuck colors, a couple of solid
horizontal lines, a scatter of colored dots, "rotating solid colors"
that don't match any of our own screens. This is what an undriven or
under-powered HUB75 panel looks like; it is NOT a sign of a firmware
crash.

**Fix:** connect a separate 5V power supply to the board/panel, not
just the USB cable, especially when testing at high brightness. Use
`diagnostics/Hub75Test/Hub75Test.ino` (a minimal sketch — default
`HUB75_I2S_CFG` config, cycles R/G/B/white full-screen) to check display
+ power in isolation from all of PixelPop's own logic. This diagnostic
sketch is still a plain Arduino IDE sketch (not part of the PlatformIO
port) — open it directly in Arduino IDE, or flash its pre-built
`Hub75Test.ino.merged.bin` with the Arduino-bundled esptool:
```bash
PORT=/dev/cu.usbmodem1101
ESPTOOL=$(ls -d "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py"/*/esptool | tail -1)
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0x0 Hub75Test.ino.merged.bin
```

### 3. A silent hang after just one boot line means the hang is in *our* code, before `Serial` output — bisect it with checkpoints

**Symptom:** Serial Monitor shows exactly one harmless ESP-IDF line
(`esp_core_dump_flash: No core dump partition found!` — this is normal;
our `partitions.csv` has no coredump partition on purpose) and then
total silence forever. No "PixelPop" Wi-Fi network ever appears.

This line comes from ESP-IDF's own early startup log (before
`app_main()`/`setup()` even runs), so seeing it proves nothing about
whether our code ran at all.

**How to debug it:** add a tiny checkpoint helper at the top of
`src/main.cpp` (the entry point — this used to be `PixelPop.ino` before
the PlatformIO port) and call it after every meaningful step of
`setup()` (and, if the hang turns out to be inside `App::begin()`,
inside that too):

```cpp
static void CK(const char *label) {
  Serial.print("CK: ");
  Serial.println(label);
  Serial.flush();
}
```

Call `Serial.begin(115200); delay(300);` as the very first lines of
`setup()`, before anything else, then `CK("...")` after each
`PLACE()`/module construction and each `app.add()`. Rebuild (`pio run`),
reflash (`pio run -t upload`), and read the last `CK:` line before
silence — that tells you exactly which step hangs. This is much faster
than guessing. Remove the checkpoints once the real cause is found and
fixed.

Do NOT assume it's PSRAM without testing: PSRAM was ruled out on this
project by rebuilding with PSRAM set to OPI, QSPI (produces an explicit
`quad_psram: PSRAM chip is not connected, or wrong PSRAM` error — proves
QSPI is the wrong PSRAM protocol for this board, if it has PSRAM at
all), and Disabled — all three still hung identically, so PSRAM was not
the cause here. Likewise this board's IMU (QMI8658, detected at I2C
address 0x6B) was ruled out — `App::begin()`'s `orientationBegin()` call
does NOT hang on this board revision.

### 4. A module's source folder can end up completely empty on disk — this looks like a code bug but isn't

**Symptom:** compile error like
`src/modules/reminders/Reminders.h: No such file or directory`
even though `src/main.cpp` has always included that header and nothing
you changed touches it.

**What actually happened once:** `src/modules/reminders/` had zero
files in it (no `.h`/`.cpp` at all) even though it's a real, wired-in
module. Cause unknown — possibly an interrupted sync from another tool
or agent working on this same project folder. It was NOT something this
agent's edits caused (verified: the missing files predated this
session's edits to that folder).

**Before assuming a real code bug**, check every module folder actually
has files matching what `src/main.cpp` includes:

```bash
for d in "$HOME/mnt/Matrix/PixelPop/src/modules/"*/; do
  echo "$(find "$d" -type f | wc -l)  $d"
done
```

A `0` next to a folder that IS `#include`d in `src/main.cpp` is data
loss, not a compile bug — restore those files from your own working
copy / backup / git history rather than trying to "fix" a compile
error that isn't really about code.

### 5. `src/core/App.cpp` (and other core files) can be more advanced on the Mac than any agent's cached copy — always re-read before touching

This project's core files get edited directly on the Mac (by the user,
by other agents/tools, or by hand in the Arduino IDE) independent of
whatever a given agent session has cached or synced into its own
scratch copy. As of this session, `App.cpp`/`App.h` on the Mac include
features that an earlier cached copy did not know about: crash-log
recording (`CrashLog.cpp/h`), IMU auto-orientation
(`Orientation.cpp/h`), a startup sound (`StartupSound.cpp/h`), and a
splash screen (`SplashLogos.h`) — all wired into `App::begin()` early,
before Wi-Fi.

**Rule:** before editing any `src/core/*` file, `cat`/`Read` it fresh
from the Mac (`$HOME/mnt/Matrix/PixelPop/...` via `device_bash`, or the
real path directly) — never assume a scratchpad/cached copy is current,
and never blind-overwrite a core file with an older cached version.
`src/main.cpp` and `src/modules/*` are lower-risk (they change less
often and this agent has been the one maintaining them this session),
but core files are not.

### 6. Terminal.app can only be clicked, not typed into, by computer-use tools

If you have computer-use access to the Mac but Terminal.app is granted
in "click" tier (`computer_resolve_access` reports
`tier: "click", isSentinel: true`), you cannot type or send keystrokes
into a real Terminal window. Either:
- give the user the exact command to paste and run themselves, or
- run it yourself via the `device_bash` tool (a real shell on the Mac,
  scoped to connected folders) when the command only touches files in a
  connected folder and doesn't need a live USB serial port — `esptool`
  writes to `/dev/cu.usbmodem*` are NOT reachable from `device_bash`
  (confirmed: no `/dev/cu.*`/`/dev/tty.*` device nodes exist in that
  VM), so actual flashing commands must go through the user's real
  Terminal.

### 7. [Only applies to Arduino IDE — the diagnostics/*.ino sketches or the Matrix Arduino backup] Arduino IDE tab clicks are unreliable via accessibility automation

PixelPop's own build no longer touches Arduino IDE (see the top of this
file), so this only matters if you're working with the standalone
`diagnostics/*.ino` sketches (pitfall #2) or the `Matrix Arduino` backup.

Clicking a document tab or the "Output"/"Serial Monitor" tab by
coordinate via `computer_app_click` often reports success (`AXPress`)
but visibly does nothing — a known Electron/Theia custom-tab-control
quirk. Workarounds that DO work:
- Toggle the Serial Monitor panel with
  `computer_app_menu({ path: ["Tools", "Serial Monitor"] })` instead of
  clicking its tab.
- The tab's "X" (close) button has a real accessible action and
  responds to `computer_app_click` reliably.
- You usually don't need to switch to `PixelPop.ino`'s tab at all before
  Verify/Export — those actions operate on the whole sketch regardless
  of which tab is frontmost.
- **Never send `cmd+w`/`Close` to the Arduino IDE window** to try to
  close a single tab — on this Electron build it closed the entire
  application (not just the tab), losing window state. If that happens,
  relaunch with `computer_open_application("cc.arduino.IDE2")` and
  reopen via `File > Open Recent > PixelPop`.

### 7.5. Permanent rule for any agent, on any board, in this project — read before touching a board

This project is worked on by more than one AI agent (Claude and
Copilot, at least) sharing the same Mac and the same physical boards,
often without either one knowing the other is active. That has already
caused real damage twice: an unattended OTA push froze Board 1 with
pixel-artifact garbage (pitfall #8), and separately a confirmed-working
build got overwritten by another agent's flash before anyone could
verify it, then that overwrite itself may have contributed to the board
going fully blank/silent. Both were avoidable. The rule, for every
agent, every time, no exceptions:

1. **Never flash, OTA-install, or erase a board without the user's
   explicit go-ahead in that specific conversation**, even if a
   previous conversation (yours or another agent's) already discussed
   or approved something similar. Approval does not carry over between
   sessions or between agents.
2. **Before flashing, check whether another agent might be mid-task on
   the same board.** Read `COPILOT_HANDOFF.md` in this folder (or leave
   your own equivalent note) — if it says another agent is actively
   driving recovery, stop and ask the user rather than acting anyway.
3. **Before overwriting what's currently on a board, back it up** —
   at minimum keep the exact `.bin`/bootloader/partitions/boot_app0
   files you're about to replace, alongside what you're writing, so a
   "no wait, put it back" is a five-second esptool command, not an
   archaeology project. `firmware-backups/board-<mac>-<timestamp>/`
   (see `COPILOT_HANDOFF.md`) is a fine convention to reuse.
4. **After flashing, say what you flashed and why**, in whatever
   handoff note this project's agents are using, so the next agent
   (or the next you, next session) doesn't have to reverse-engineer it
   from file timestamps.

### 8. Never let the OTA "Build, Publish, and Install" workflow below run unattended

**What happened once:** something (an agent, or the `bpr` shortcut) ran
the OTA publish workflow further down this file on its own — compiling
the sketch, pushing the `.bin` to the GitHub repo, then POSTing
`auto=on` to `/advanced/autoupdate` and hitting `/advanced/autocheck` —
with nobody watching the board and no confirmation that the exact build
being pushed had ever been tested on real hardware over USB first. The
board pulled it over Wi-Fi, installed it, rebooted, and came up frozen
showing pixel-artifact garbage. (An "auto=on" flip also leaves the
board rechecking that same URL every 15 minutes on its own — see
`fwAutoTick()`/`AUTO_CHECK_MS` in `FwUpdate.cpp` — so it can keep
reinstalling a bad build even after the first failure.)

**Rule:** never trigger the OTA publish/install workflow below (and
never run `bpr`) unless (a) the exact `.bin` has already been confirmed
working via a manual USB flash on real hardware, per steps 1-3 above,
and (b) a person is watching the board when the OTA install lands, so a
bad result is caught immediately instead of leaving the board bricked
with nobody noticing. If you find automatic updates already switched on
and you didn't do it, turn it back off first —
`curl -fsS -X POST --data 'auto=off' http://<device-ip>/advanced/autoupdate`
(or the Advanced page's toggle) — before touching anything else.

**Recovery:** identical to "Existing board recovery" below — rebuild
(`pio run`) and reflash (`pio run -t upload`). Do not try to fix a bad
OTA install with another OTA install: if the board isn't answering web
requests, USB is the only path back.

### 9. Wrong colors (gold/orange/yellow show pink or magenta) = wiring-order setting, not hardware

**Symptom:** Weather's condition text and Forecast's day names (gold `0xFFD200`)
show pink/magenta; Color Lab's Orange/Yellow swatches look pink/magenta; green
shows as blue and blue as green. Clouds and white text can look nearly right,
which makes it easy to misdiagnose.

**Cause:** these panels have green and blue swapped in hardware. The per-board
wiring-order setting (NVS key `cord`, Advanced > Color Lab) must be
**"Swap green and blue"**, which is also the firmware default. "Normal" produces
exactly this symptom. The setting survives reflashing, so two boards on
identical firmware can disagree.

**Fix:** Advanced > Open Color Lab > wiring = "Swap green and blue" > Save > Done.
Before doing any other color debugging, compare the Advanced page "Currently:"
line across boards. On 2026-09-25 this cost hours of chasing cables, panels,
clock phase and color depth before it was found. None of those were the cause.

### 10. `esptool read-flash` over native USB drops out on long reads

Full or multi-MB `read-flash` backups fail with "Serial data stream stopped:
Possible serial noise or corruption", often at the same address every time.
Use a short, good data cable plugged straight into the Mac, `--baud 115200`,
and read the app region in 1 MB chunks (one esptool call each), then
`ls -la` the chunks to check every one is exactly 1048576 bytes **before**
you `cat` them together. `cat` silently skips missing files. Writes
(`write-flash`) were not affected.

### 11. [HISTORICAL — pre-PlatformIO] This project used to have several stale `build/*` folders

Before the PlatformIO port, `build/esp32.esp32.esp32s3/` was Export
Compiled Binary's output, and several other `build/*` folders
(`current`, `restore`, `stage-one`, `stage-two`, `usb`, `weather-only`,
`weather-internal`, `weather-internal-strict`) were stale leftovers from
the OTA publish workflow and one-off test compiles — all deleted as
part of the 2026-09-25 port. `build/` now only holds the
`canvas-test`/`hub75-test` diagnostic sketch outputs (pitfall #2), which
are unrelated to the main firmware. **PlatformIO's own build output
lives in `.pio/build/pixelpop/`** (`bootloader.bin`, `firmware.bin`,
`partitions.bin`) — `pio run` and `pio run -t upload` manage that
folder themselves, so there's no equivalent stale-folder risk to check
for; just re-run `pio run` if in doubt, it's fast after the first build.

On this Mac, plain `esptool.py` and `esptool` are also not on PATH —
`pio run -t upload` doesn't need you to find it (PlatformIO manages its
own copy), but the Arduino-bundled one is still needed for the
`diagnostics/*.ino` sketches in pitfall #2:

```bash
ESPTOOL=$(ls -d "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py"/*/esptool | tail -1)
```

### 12. PlatformIO's registry doesn't have every library under the name you'd guess

`lib_deps = mrcodetastic/ESP32-HUB75-MatrixPanel-DMA@3.0.14` fails with
`UnknownPackageError` — that library isn't published under that exact
slug in PlatformIO's registry, even though it installs fine by that
name in the Arduino IDE's Library Manager. **Fix:** point `lib_deps` at
the GitHub repo directly, pinned to a tag, instead of a registry name:

```
lib_deps =
    https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA.git#3.0.14
```

If a future library hits the same `UnknownPackageError`, check its
`library.properties`/`library.json` for a `url=` line and use that
GitHub URL the same way rather than guessing at owner/name variations.

### 13. `pio run -t upload` fails with "No serial data received" — native USB auto-reset quirk, not a real connection problem

**Symptom:**
```
A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.
```
even though the board is plugged in and the port is correct.

**Cause:** esptool's automatic reset-into-bootloader sequence (toggling
RTS/DTR) doesn't always work reliably over the ESP32-S3's native USB
CDC, unlike a classic UART-USB bridge. (Confirmed on this project;
also confirmed this is unrelated to the board simply not being plugged
in — check that first, it's the more common cause in practice.)

**Fix:** manually put the board in bootloader mode before uploading —
hold **BOOT**, tap and release **RESET** while still holding BOOT, then
release BOOT — then immediately run `pio run -t upload`. If it's a
recurring annoyance, check whether the board enumerates two USB
devices (`ls /dev/cu.usbmodem*`) and pass the right one explicitly with
`--upload-port`.

---

## Standing project instructions (carry these forward)

- Write over files directly — no need to ask before editing.
- Shell commands may be run without asking.
- Do NOT update `PixelPop User Guide.html`, `CHANGELOG.md`, or the two
  "How to…" `.md` files unless the user explicitly asks for that.
- Never overwrite the real `src/modules/flight/LogoData.cpp` or
  `src/core/GuideData.cpp` — large generated files.

---

## Build, Publish, and Install Over Wi-Fi (OTA)

This section was written by an earlier agent (GitHub Copilot), has
**not** been independently verified end-to-end, and as of the
2026-09-25 PlatformIO port is now also **known stale**: it calls
`arduino-cli compile` against the retired Arduino IDE project layout,
which no longer exists in this live copy (no `.ino`, no `sketch.json`).
This has NOT been ported to PlatformIO or re-verified — treat it as
reference only until someone rewrites the compile step to
`pio run` and confirms the rest still works. Do not run this as-is.

Set `PUBLISH` to the checkout that hosts the published firmware binary
and `IP` to the device's normal network address:

```bash
PUBLISH="/Users/raul/.copilot/session-state/ec6fb6eb-2c0c-4997-8ba7-feeaab765ccd/pixelpop-publish"
IP="192.168.6.189"

arduino-cli compile --output-dir build/current \
  --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=app13M_data7M_32MB,PSRAM=opi' . && \
cp build/current/PixelPop.ino.bin "$PUBLISH/PixelPop.ino.bin" && \
git -C "$PUBLISH" add PixelPop.ino.bin && \
git -C "$PUBLISH" commit -m 'Update firmware binary' && \
git -C "$PUBLISH" push origin main && \
curl -fsS -X POST --data 'auto=on' "http://$IP/advanced/autoupdate" && \
curl -fsS -X POST "http://$IP/advanced/autocheck"
```

Poll update progress with:

```bash
while true; do
  curl -fsS "http://192.168.6.189/advanced/fstatus"
  echo
  sleep 2
done
```

A `bpr` zsh function reportedly wraps this same workflow
(`bpr --progress` for a monitored run;
`PIXELPOP_DEVICE_IP=... bpr` to override the target device). This has
not been tested by this agent — verify it still exists and works before
depending on it.

## Existing board recovery

For an existing board with a damaged OTA boot selection, rebuild
(`pio run`) and reflash (`pio run -t upload`). The OTA metadata image
at `0x19000` is required — it selects the application at `0x20000` as
the next firmware to boot; PlatformIO writes it automatically as part
of `pio run -t upload`, you don't need to track it as a separate file.
