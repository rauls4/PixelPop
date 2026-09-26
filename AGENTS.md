# Agent onboarding — read this first

This project is actively worked on by more than one AI coding agent
(Claude, GitHub Copilot, and possibly others), sharing this same Mac
and these same four physical ESP32-S3 boards. Read this file before
you write any code or touch any board.

## Read these two files before doing anything else

1. `COPILOT_HANDOFF.md` — current project status, what's resolved,
   what's in progress, and any active hold on a board another agent
   is working on.
2. `FLASHING.md` — the build/flash procedure and a "Known pitfalls"
   section covering hours of hard-won debugging. Read it before
   debugging anything that looks like a hardware fault; several past
   incidents that looked like hardware were actually a firmware
   setting or a build-tool bug.

## Build system

This project builds with **PlatformIO** (`platformio.ini` at the
project root, entry point `src/main.cpp`). It does NOT use Arduino IDE
or `arduino-cli` any more (ported 2026-09-25) — there is no `.ino` file
or `sketch.json` in this live copy. If you're about to run `arduino-cli`
or open this folder in Arduino IDE, stop: it won't find a sketch here.
A full pre-port Arduino IDE copy is kept at `~/Desktop/Sketches/Matrix
Arduino` for reference only — it is not the live project, don't edit it
expecting changes to reach the boards.

Build: `pio run`. Flash: `pio run -t upload`. See `FLASHING.md` for the
full procedure and known gotchas (library registry naming, the native
USB auto-reset quirk, etc).

## The one rule that matters most: coordinate before touching a board

Two agents have already stepped on each other on this project — once
an unattended OTA push froze a board with pixel-artifact garbage, once
a confirmed-working flash got silently overwritten before it could be
verified. Both were avoidable. Every agent, every session, no
exceptions:

1. **Never flash, OTA-install, or erase a board without the user's
   explicit go-ahead in *that* conversation.** Approval doesn't carry
   over from a previous session or a different agent.
2. **Before flashing, check `COPILOT_HANDOFF.md`** for a note that
   another agent is actively driving recovery on a board. If one
   exists, stop and ask the user rather than acting anyway.
3. **Before overwriting what's on a board, back it up** — at minimum
   the exact images you're about to replace.
4. **After flashing, write down what you flashed and why** in
   `COPILOT_HANDOFF.md`, so the next agent (or the next session of you)
   isn't reverse-engineering it from file timestamps.
5. **Never run the OTA "Build, Publish, and Install" workflow
   unattended.** It's also currently stale (still calls `arduino-cli`)
   — see `COPILOT_HANDOFF.md`.

## Files never to overwrite

`src/modules/flight/LogoData.cpp` and `src/core/GuideData.cpp` are
large generated files — never regenerate or blind-overwrite them.

## Docs to only touch if the user asks

`PixelPop User Guide.html`, `CHANGELOG.md`, and the two "How to…" `.md`
files. `FLASHING.md` and `COPILOT_HANDOFF.md` are the exception — keep
those current as you work; they're the shared memory between agents on
this project.
