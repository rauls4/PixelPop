# `updatePP` — build, flash, publish, and install PixelPop firmware

A single zsh function you add to your own `~/.zshrc` (not part of the firmware
itself — nothing in this repo depends on it). It always runs from
`~/Desktop/Sketches/Matrix/PixelPop`, regardless of what directory you're in
when you type it. It replaces the old separate `ppp` and `ppu` functions —
everything they did is now one command.

A full man page ships alongside it: see `updatePP.1` in this same folder, and
"Installing the man page" below.

## Setup (one time)

Paste into your terminal:

```bash
echo '
updatePP() {
  setopt local_options 2>/dev/null
  unsetopt xtrace verbose 2>/dev/null

  local do_flash=0 do_upload=0 device=""
  local OPTIND opt
  while getopts "fuo:" opt; do
    case "$opt" in
      f) do_flash=1 ;;
      u) do_upload=1 ;;
      o) device="$OPTARG" ;;
      *) echo "usage: updatePP [-f] [-u] [-o <device-ip-or-hostname>]"; return 1 ;;
    esac
  done

  if [ "$do_flash" = "1" ] || [ "$do_upload" = "1" ] || { [ -z "$device" ] && [ "$do_flash" = "0" ] && [ "$do_upload" = "0" ]; }; then
    cd ~/Desktop/Sketches/Matrix/PixelPop || return 1
    pio run || return 1

    if [ "$do_flash" = "1" ]; then
      pio run -t upload || return 1
    fi

    if [ "$do_upload" = "1" ]; then
      cp .pio/build/pixelpop/firmware.bin PixelPop.ino.bin || return 1
      git add PixelPop.ino.bin
      if git diff --cached --quiet; then
        echo "No firmware changes since the last push."
      else
        git commit -m "Update firmware binary" || return 1
        git push origin main || return 1
        echo "Pushed."
      fi
      echo ""
      echo "To install on a board, run:"
      echo "  updatePP -o <device-ip-or-hostname>"
    fi
  fi

  if [ -n "$device" ]; then
    echo "Starting install on $device..."
    local resp
    resp=$(curl -fsS -m 10 -X POST "http://$device/update/install") || { echo "Could not start update."; return 1; }

    local width=30
    while true; do
      local st
      st=$(curl -fsS -m 10 "http://$device/advanced/fstatus") || {
        printf "\n"
        echo "The display stopped answering. If it restarted, the update is installed."
        return 0
      }
      local state pct msg rest filled empty bar
      state="${st%%|*}"
      rest="${st#*|}"
      pct="${rest%%|*}"
      msg="${rest#*|}"
      [ -z "$pct" ] && pct=0

      filled=$(( pct * width / 100 ))
      empty=$(( width - filled ))
      bar=$(printf "%*s" "$filled" "" | tr " " "#")
      bar+=$(printf "%*s" "$empty" "" | tr " " "-")
      printf "\r[%s] %3d%%  %s\033[K" "$bar" "$pct" "$msg"

      case "$state" in
        done)
          printf "\n"
          echo "Done — the board is restarting into the new build. Watch it."
          return 0 ;;
        fail)
          printf "\n"
          echo "Update failed: $msg"
          return 1 ;;
      esac
      sleep 1
    done
  fi
}' >> ~/.zshrc
source ~/.zshrc
```

After that, `updatePP` works in any new terminal window/tab.

If you still have the old `ppp` and/or `ppu` functions in `~/.zshrc` from
before, remove them — see "Removing the old `ppp`/`ppu` functions" below for a
script that does it automatically.

## Options

| Command                          | Does |
| ---                               | --- |
| `updatePP`                        | Builds only (`pio run`). No flashing, no publishing, no install. |
| `updatePP -f`                     | Builds, then flashes the board plugged in over USB (`pio run -t upload`). Put the board in bootloader mode manually (hold BOOT, tap RESET, release BOOT) if you hit "No serial data received"; see `FLASHING.md` pitfall #13. |
| `updatePP -u`                     | Builds, then copies `.pio/build/pixelpop/firmware.bin` to `PixelPop.ino.bin` at the repo root and pushes it to the `rauls4/PixelPop` GitHub repo's `main` branch. If the binary hasn't changed since the last push, it says so instead of failing. Always ends by printing the `updatePP -o` command to run next. |
| `updatePP -fu`                    | Build, flash locally, and push to GitHub, all in one step. |
| `updatePP -o <device>`            | Skips the build entirely. Tells `<device>` (an IP or a `.local` hostname) to download and install whatever is currently published on GitHub, then watches its progress with a bar until it finishes or fails. |
| `updatePP -uo <device>`           | Builds, publishes to GitHub, and immediately installs that freshly-published build on `<device>` — build through install in one command. |
| `updatePP -fuo <device>`          | All of the above: build, flash the USB board, publish to GitHub, and install on `<device>`. |

`-f`, `-u`, and `-o <device>` can be combined in any order (`-fu`, `-uo host`,
`-fuo host`, ...). `-o` always runs last, after any build/flash/publish steps
requested alongside it.

## What `-u` does and does not do

`-u` only pushes the compiled binary to GitHub. It does **not** touch any
board, does **not** flip a board's auto-update setting on, and does **not**
trigger an OTA install anywhere by itself — that's what `-o` is for, as a
separate, explicit step.

This is intentionally narrower than the old "Build, Publish, and Install Over
Wi-Fi (OTA)" workflow documented (and marked stale) in `FLASHING.md`: that
workflow also flipped auto-update on and POSTed to a board's
`/advanced/autocheck`, which is a *sticky* change (the board then keeps
re-checking every 15 minutes indefinitely) and is exactly the sequence that
froze a board once before — see `FLASHING.md` pitfall #8. `-o` is a one-shot,
one-time install you trigger yourself while watching the board — it never
touches the board's own auto-update setting.

## `-o` — install a published build on a board

The part that used to be the standalone `ppu` command. Once a binary is on
GitHub (via `-u`, earlier or in the same command), `-o <device>` tells that
one board to download and install it right now, and watches it finish. It's
the terminal equivalent of opening the board's home page and clicking the
"update available" banner's Install button.

Usage: `updatePP -o 192.168.6.189` or `updatePP -o pixelpop-livingroom.local`.

`-o` hits `/update/install` directly — it does not touch the Advanced page's
"automatic updates" checkbox, so there's no lingering state to remember to
turn off afterward. Same standing rule as everything else here: **watch the
board while it runs.** It downloads, installs, and reboots on its own; if
anything looks wrong afterward, `updatePP -f` gets you back to a known-good
state over USB.

## Installing the man page

`updatePP.1` in this folder is a standard Unix man page for the command. To
make `man updatePP` work in any terminal:

```bash
mkdir -p ~/.local/share/man/man1
cp ~/Desktop/Sketches/Matrix/PixelPop/updatePP.1 ~/.local/share/man/man1/
echo 'export MANPATH="$HOME/.local/share/man:$MANPATH"' >> ~/.zshrc
source ~/.zshrc
man updatePP
```

(If `~/.local/share/man` is already on your `MANPATH` — check with
`manpath`— you can skip the `export` line.)

## Removing the old `ppp`/`ppu` functions

If `~/.zshrc` still has the earlier separate `ppp` and/or `ppu` functions,
paste this to remove them (safe to run even if one or both are already gone):

```bash
python3 - << 'SCRIPT'
import pathlib, re

p = pathlib.Path.home() / ".zshrc"
text = p.read_text()
orig = text

def strip_function(text, name):
    m = re.search(name + r'\(\) \{', text)
    if not m:
        return text
    start = m.start()
    i = text.index('{', start)
    depth = 0
    j = i
    while True:
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                break
        j += 1
    end = j + 1
    return text[:start] + text[end:]

text = re.sub(r'^ppp\(\) \{ cd ~/Desktop/Sketches/Matrix/PixelPop && pio run; \}\n', '', text, flags=re.MULTILINE)
text = strip_function(text, 'ppp')
text = strip_function(text, 'ppu')
text = re.sub(r'\n{3,}', '\n\n', text)

p.write_text(text)
print(f"Done. {len(orig)} -> {len(text)} bytes.")
SCRIPT
source ~/.zshrc
```

## Repo setup (already done, 2026-09-25)

`~/Desktop/Sketches/Matrix/PixelPop` is itself a git checkout of
`rauls4/PixelPop` (`origin/main`). Before this, that GitHub repo only ever
held `PixelPop.ino.bin` — no source was pushed, and `-u` keeps it that way:
it stages and commits only `PixelPop.ino.bin`, never `git add`-ing anything
else. A `.gitignore` at the repo root (covering `.pio/`, `build/`,
`firmware-backups/`, `archive/`, and OS/editor cruft) is there as a safety
net in case anyone ever runs a broader `git add` by hand in this folder.
