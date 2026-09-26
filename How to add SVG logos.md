# How to add SVG logos

The flight tracker shows a small airline logo (16 x 16 pixels, top left) next to a
flight's data. The logos are not read from files while the board runs. They are
converted once, on your computer, into a data file that is built into the sketch:

    src/modules/flight/LogoData.cpp     (generated - never edit it by hand)

The converter is `tools/make_logos.py`. It accepts **SVG**, BMP and PNG files.

## 1. Name the file after the airline

Use the airline's 3-letter ICAO code, in capitals or lowercase:

    UAL.svg     United
    DAL.svg     Delta
    AAL.svg     American

A flight gets a logo when its callsign starts with that code followed by a digit
(UAL1234 uses `UAL`). Private and other flights without an airline code just show
the text layout with no logo.

If a code has both an SVG and a bitmap (`AAL.svg` and `AAL.bmp`), the SVG is used.
If the SVG cannot be drawn, the bitmap is used instead and the script tells you.

## 2. Put the files in the logos folder

Your logos live in:

    /Users/raul/Downloads/FlightTracker-main/assets/airlines/airline_logos_16

Add or replace the `.svg`, `.png`, or `.bmp` files there. The checked-in logo
data currently comes from this folder. SVGs should be roughly square. A non-square
SVG is fitted inside the square and centered; bitmap images must already be square.
Logos with a transparent background work too (see "Background color" below).

## 3. One-time setup on your Mac

Open Terminal and run these one at a time:

    brew install imagemagick
    python3 -m venv ~/logo-env
    source ~/logo-env/bin/activate
    pip install pillow

- **ImageMagick** draws the SVGs. (The script can also use `cairosvg`,
  `rsvg-convert` or Inkscape, if you already have one of them.)
- **Pillow** is the Python image library the script needs.
- The `venv` step is needed because Homebrew's Python refuses to install packages
  system-wide ("externally managed environment"). The environment keeps everything
  in `~/logo-env`.

## 4. Convert the logos

Each time you open a new Terminal window, first activate the environment:

    source ~/logo-env/bin/activate

Then run the converter, giving it the full path of the script and of the logos folder:

    python3 "/Users/raul/Desktop/Sketches/Matrix/PixelPop/tools/make_logos.py" "/Users/raul/Downloads/FlightTracker-main/assets/airlines/airline_logos_16"

It rewrites `src/modules/flight/LogoData.cpp` and prints a summary such as:

    drawing SVGs with ImageMagick (convert)
    logos: 815 (1 from SVG)  size: 16x16  distinct pictures: 749  data: 301974 bytes

Any file it could not use is listed as `skipped`, with the reason.

## 5. Upload the sketch

Open `PixelPop.ino` in the Arduino IDE and upload as usual. New logos appear
the next time a flight with that airline's code is shown.

## Options

Add these after the folder name:

- **Background color**: `--bg white` (default), `--bg black`, or `--bg "#003366"`.
  This fills transparent areas of SVG and PNG logos. Black lets the panel's own
  black show through, which suits logos designed for dark backgrounds.
- **Size**: a number after the output file name. For example, 24 makes 24 x 24
  logos. The screen layout is designed for 16, so a bigger size needs layout changes
  in `src/modules/flight/Flight.cpp`.
- **Output file**: the second argument, if you want the data written somewhere else.

Full form:

    python3 make_logos.py <logos folder> [output.cpp] [size] [--bg color]

You can also adjust how bright the logos appear on the Flights page of the settings
website ("Logo brightness"), and turn them off there.

## Troubleshooting

**"can't open file '/Users/raul/tools/make_logos.py'"**
Terminal was in your home folder, so the short path did not work. Use the full path
shown in step 4.

**"No module named 'PIL'"**
Pillow is not installed in the Python you are running. Activate the environment
(`source ~/logo-env/bin/activate`) and run `pip install pillow`.

**"externally-managed-environment"**
Do not install packages into Homebrew's Python directly. Use the `venv` from step 3.

**"SVG files found, but nothing to draw them with"**
Run `brew install imagemagick` and try again.

**A logo shows the wrong colors or looks empty**
Some SVGs use features the drawing tools do not support (filters, embedded fonts,
masks). Open the SVG in a browser to check it. In an editor such as Inkscape, convert
any text to paths and save as a plain SVG. As a fallback, export a square PNG or BMP
and use that.

**A logo is missing on the panel**
Check that the file name is exactly the 3-letter code the callsign starts with, that
the script listed no `skipped` line for it, and that you uploaded the sketch again
after running it.

## Adding logos in bulk

Drop all the new files in the folder and run the converter once. It processes
everything each time, so there is no need to run it per file. Identical pictures are
stored only once, and the whole set of about 800 logos takes about 300 KB of the
board's flash.
