# PixelPop changelog

- Retired the Chicago This Week and Color Fractal modules.
- Firmware 1.1.22 restores Waveshare's official HUB75 pin map.
- Firmware 1.1.23 disables HUB75 double buffering for reliable output on the Waveshare board.
- Firmware 1.1.5 clears and darkens both panel DMA buffers around OTA restarts to prevent boot-time color artifacts.
- Fire 1.1.3 lowers each roast item to a randomized low flame height while keeping the food level on its stick.
- Fire 1.1.4 adds a shaded, rising smoke puff while the burned food breaks into ash.
- Fire 1.1.5 randomly alternates whether each roast item is held from the left or right side of the display.
- Fire 1.1.6 restores the simple Fire 1.1.0 roasting cycle with 2x-sized, level food that stops at random low flame heights and is held from a random side.
- Fire 1.1.7 restores rounded, shaded marshmallow and sausage silhouettes at the larger size.
- Fire was restored to its original 1.1.0 roast behavior and version.
- Plex 2.1.0 caches the server's low-resolution art preview and displays it in the unused portrait space.
- Plex 2.1.1 uses this Plex server's compatible photo-transcoder endpoint for portrait poster previews.
- Plex 2.2.0 defaults to showing only unwatched movies; the redundant unwatched dog-ear is hidden while that filter is active.
- Plex 2.2.1 requests full-width portrait art previews and anchors them below the movie details.
- Plex 2.3.0 removes the added-age display and setting.
- Plex 2.3.1 leaves a four-pixel gap below the rating row before the portrait poster begins.
- Plex 2.3.2 uses TomThumb for the portrait category row.
- Plex 2.3.3 compresses the portrait category and rating rows, expanding poster artwork to 40 pixels tall.
- Plex 2.3.4 lowers the portrait poster by one pixel while preserving its full visible height.
- HAL 1.2.10 increases terminal marquee speed while preserving the existing lens pulse speeds.
- HAL 1.3.0 adds a persisted slider for terminal scroll speed.
- Countdown 1.3.5 retains its last confirmed availability during a transient system-time read failure, preventing loading-spinner flashes.
- Forecast 1.2.10 uses a black portrait marquee field for stronger scrolling-text contrast; Weather 1.6.9 removes the condition header background.
- Forecast 1.3.0 gives day names the same compact gold-on-black treatment as Weather conditions.
- Wi-Fi connection startup now uses the supplied 32-pixel inverted signal logo.
- Startup splash now uses the supplied native-resolution monochrome PixelPop logos for landscape and portrait.
- Firmware 1.1.24 adds the device's local `.local` hostname as a third card on the landscape network information screen, matching portrait's existing Wi-Fi/IP/local-name display.
- Firmware 1.1.25 adds a live full-panel preview on the display when hovering a function's row on the settings site.
- Countdown 1.3.6 also shows the animated hourglass in portrait, centered in its own row under the divider.

Every function (module) has its own version, `MAJOR.MINOR.PATCH`, set in its header with
`version()`. The firmware as a whole has `FW_VERSION` in `src/core/Config.h`. Both are shown on the
settings site: each function's page has its version next to its name, and the **Advanced** page lists
them all.

How to number a change:

- **PATCH** (1.0.0 to 1.0.1): a fix, no new settings.
- **MINOR** (1.0.0 to 1.1.0): something new, or a new setting.
- **MAJOR** (1.0.0 to 2.0.0): saved settings from an older version would not work any more.

## Current firmware

- Portrait IP display uses compact scrolling rows for the Wi-Fi network, IP address, and device `.local` name.
- Landscape network information gives the Wi-Fi name and IP address separate full-width cards, centered when possible and starting their marquee visibly.
- Portrait firmware-update screen scrolls `UPDATING FIRMWARE` in the compact font.
- Portrait system messages use the compact font; firmware update completion shows fully readable `UPDATE` and `RESTART` labels.
- Wi-Fi setup now starts with a white Wi-Fi signal glyph on black before showing the setup address.
- Added Chicago This Week, which shows upcoming Chicago Park District activities from the public City of Chicago data catalog.
- Fixed the shared network worker capacity so Chicago This Week and all other network modules receive refresh time.
- Chicago This Week now retries as soon as time sync completes and displays its loading or connection status instead of hiding itself.
- Advanced offers an explicit opt-in, one-shot updater that checks and installs only the official published PixelPop firmware without risking an update/reboot loop.
- Saving unchanged Home-page General settings no longer restarts PixelPop; a restart occurs only after changing its network name.
- Added a Home-page app-wide 24-hour-time preference used by Clock, Calendar, Chicago This Week, and the sleep clock.
- Expanded portrait Arcade Tetris to a 20-row, nearly full-height playfield; live score text is no longer shown.
- Fixed In Season advancing early after its last item scrolled off; it now honors its configured display duration.
- Replaced animated Weather and Forecast icons with static weather icons.
- Rotation now keeps the active module selected when portrait and landscape expose different page counts.
- Aligned portrait Weather with Forecast's compact header, icon, and footer layout; its temperature now uses the tiny 3×5 display font. Clock now defaults to 15 seconds per rotation.
- Added an audible, non-blocking descending bloop when the display rotates; it yields to active alerts, chimes, games, and fireworks.
- OTA installation now explicitly selects and verifies the downloaded partition as the next boot target before restarting.
- Wi-Fi/network information remains visible for 20 seconds; connection start uses a signal icon.
- Firmware access codes are centered; the completed splash remains visible for five seconds without a startup-status label.
- Background application tasks run on core 1, reserving core 0 for ESP32 Wi-Fi and watchdog-critical system work.
- Weather and Forecast now use shaded, layered icons with cloud depth, sun highlights, colored rain, and a shaded moon.
- Landscape Weather now uses a compact static dashboard: condition header, temperature and icon, plus a readable Feels/Wind/Humidity footer.
- Landscape Weather's detail footer now uses the 3×5 TomThumb font and scrolls cleanly across the bottom.
- Landscape Weather's scrolling detail footer is raised two pixels to prevent clipping at the display edge.
- Portrait Weather now matches the landscape dashboard palette, has a 2× temperature, a scrolling condition header, and a lower footer band behind its scrolling details.
- Weather and Forecast temperature values now display a compact degree marker.
- Portrait Forecast balances the temperature vertically, gives its degree marker one pixel more spacing, and raises its detail marquee by one pixel.
- Portrait Forecast now centers the temperature and degree marker as one measured group, with equal visual spacing above and below it.
- Two-day landscape Forecast now uses TomThumb labels and temperature values with large shaded icons in a collision-free layout.
- Two-day landscape Forecast raises its bottom temperatures and uses a solid full-height card divider.
- Firmware-update confirmation now shows the downloaded firmware version between `UPDATE` and `RESTART`.
- Landscape network cards now measure their labels before drawing so the IP address is precisely horizontally centered.
- Portrait Countdown text marquees now fully leave the screen before restarting, and its main value is centered from its rendered bounds.
- Landscape Countdown now always uses its uncluttered days-only layout.
- Portrait Countdown now also always shows days only.
- Firmware 1.1.26 shows all three network details (Wi-Fi, IP, and local name) on the landscape network information screen at once, in place of cycling through one card at a time.
- Firmware 1.1.27 adds a small eye icon to the left of each settings-site function row: closed and black for one you can preview, open while you are hovering it, grey for a disabled function, and slashed for one with no display page.
- Countdown 1.3.7 shades its animated hourglass with a lit and a shadowed tone of the glass and sand colors, plus a small glint on the glass, matching the light-from-upper-left look of the weather icons and the grandfather clock's case.

- Countdown 1.3.8 makes its animated hourglass wider (9 pixels instead of 7) so it no longer looks too thin, its reset now spins the glass a continuous 180 degrees in place (like a record on a turntable) instead of pinching an axis flat and back out, and its big day-count number now draws in a smooth font (a Helvetica-like face, antialiased) instead of the blown-up 5x7 bitmap font, picking the largest of four sizes that fits.

- Firmware 1.1.28 shows the PixelPop logo in a module's Matrix Simulator preview when that module has no dedicated live illustration there yet, instead of generic placeholder bars; and shows a disabled function's eye icon open (rather than closed) in light gray, instead of a closed dark-grey eye; and the display can now soften the stair-stepped corners of large scaled-up text (antialiasText()), for any module that wants smoother big numbers; and the on-panel firmware-update screen now shows the PixelPop logo too (in place of the plain "UPDATING" title on landscape panels with room for it, and alongside the title on portrait panels), with those landscape panels showing the bar alone and no percent text.

- Clock 1.4.2 draws its big HH:MM time in a smooth font (a Helvetica-like face, antialiased) instead of the blown-up 5x7 bitmap font.

- Weather 1.6.10 draws its big current temperature in a smooth font instead of the blown-up 5x7 bitmap font, on both the portrait card and the landscape page, with the degree mark placed from the number's actual measured width instead of an assumed one; falls back to the old bitmap font on a panel too small for it to fit.

- Weather 1.6.11 shrinks that smooth font down to its smallest size on both pages -- the larger sizes read as too big next to the rest of the card.

- Air 1.1.1 draws its big temperature and humidity numbers in the same small smooth font as Weather, on both the portrait card and the landscape page, instead of the blown-up 5x7 bitmap font; falls back to the old bitmap font when a number is too wide to fit its column.

- Ticker 1.0.3 sits the centered news headline a pixel lower in its white band.

- Plex 2.3.5 sits the landscape ratings row two pixels lower.

- Gas 1.2.4 draws its big price digits in a smooth font instead of the blown-up 5x7 bitmap font, on both the portrait card's dollar digit and the landscape page's price, keeping the small "$" and cents in their existing bitmap font either way; falls back to the old bitmap font on a panel too small or short for it to fit; and drops the landscape page's small raised third-decimal digit, rounding its price to the cent instead.

Bump the module's version whenever you change it, and add a line here.

## Firmware 1.0.0

Versioning starts here. Every function begins at 1.0.0 with everything built so far:

| Function | Version |
| --- | --- |
| Weather | 1.6.8 — portrait matches the landscape dashboard palette and 2× temperature; all temperature values include a degree marker |
| Forecast | 1.2.9 — two-day landscape raises its TomThumb temperature row and uses a full-height divider; portrait centers its degree-marked temperature as one group |
| Air Quality | 1.1.0 — portrait-first stacked readings, centered detail rows, and full-width trend graphs |
| Clock | 1.4.1 — portrait pendulum is extended so its bob just meets the digital clock |
| Countdown | 1.3.4 — settings now reflect the fixed days-only display; portrait and landscape show days only, with portrait marquees fully leaving the screen before restarting |
| Calendar | 1.1.1 — event descriptions use a light-gray panel with black text in portrait and landscape |
| Color Fractal | 1.0.0 — new animated Mandelbrot-inspired spectrum module with selectable motion speed |
| HAL | 1.2.9 — settings can enable the randomized terminal-line marquee along the bottom in portrait |
| Gas Prices | 1.2.3 — portrait uses a GA$ header and framed trend chart that turns red when the latest price is rising |
| Chimes | 1.3.1 — speaker output and I2S pin controls are tucked into a collapsible Advanced settings section |
| Fireworks | 1.0.0 |
| Arcade | 1.2.4 — landscape completes one full run-and-chase story before advancing to the next module |
| Marquee | 1.0.0 |
| News Ticker | 1.0.0 |
| Photos | 1.0.0 |
| Flights | 1.1.6 — alternates ICAO aircraft codes with resolved model names and aligns landscape heading arrows to the type row |
| Chimes | 1.1.1 — defers the speaker probe until the boot services have settled |
| HAL | 1.2.2 — landscape gives the restored left lens a distinct, bright red panel beside the typed CRT terminal |
| In Season | 1.4.1 — portrait produce illustration is larger and vertically centered |
| Lava Flow | 1.7.1 — the only module settings page that retains its live matrix preview |
| Fire | 1.1.2 — charred roasting items disintegrate into a shower of falling ash particles |
