#!/usr/bin/env python3
"""Turn a folder of airline logos into src/modules/flight/LogoData.cpp.

Usage:
    python3 tools/make_logos.py  <folder>  [output.cpp]  [size]  [--bg white|black|#rrggbb]

* Each file is named by the airline's 3-letter ICAO code: AAL.svg, UAL.svg, ...
  SVG, BMP and PNG files all work. If a code has both an SVG and a bitmap, the SVG is used.
* Pictures are scaled to `size` x `size` pixels (default 16), which is how big they
  appear on the panel. SVGs are drawn at exactly that size (drawn large, then shrunk,
  so edges stay smooth); non-square ones are centered.
* Transparent areas (SVG or PNG) are filled with --bg (default white; use black to let
  the panel's own black show through).
* Pictures are converted to the panel's 16-bit color, identical pictures are stored
  once, and the pixels are run-length compressed.
* Run it again whenever you add or change logos, then re-upload the sketch.

Needs Pillow:  pip3 install pillow
SVG files also need ONE of these to draw them (the script uses the first it finds):
    cairosvg   pip3 install cairosvg (in a virtual environment)
    rsvg-convert   (macOS: brew install librsvg)
    inkscape       (inkscape.org)
    ImageMagick (convert / magick), e.g. brew install imagemagick - the simplest choice
"""
import glob, hashlib, io, os, re, shutil, subprocess, sys, tempfile
import xml.etree.ElementTree as ET
from PIL import Image

DEFAULT_SIZE = 16
SUPERSAMPLE = 8            # SVGs are drawn this many times larger, then shrunk

# ---------------------------------------------------------------- SVG drawing
def _run(cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

def _via_cairosvg(path, width, height):
    import cairosvg
    kw = {}
    if width: kw['output_width'] = width
    if height: kw['output_height'] = height
    return cairosvg.svg2png(url=path, **kw)

def _via_cli(cmd_for):
    def render(path, width, height):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'out.png')
            _run(cmd_for(path, out, width, height))
            with open(out, 'rb') as f:
                return f.read()
    return render

def _rsvg_cmd(path, out, w, h):
    cmd = ['rsvg-convert', '--keep-aspect-ratio', '-o', out]
    if w: cmd += ['-w', str(w)]
    if h: cmd += ['-h', str(h)]
    return cmd + [path]

def _inkscape_cmd(path, out, w, h):
    cmd = ['inkscape', path, '-o', out]
    if w: cmd += ['-w', str(w)]
    if h: cmd += ['-h', str(h)]
    return cmd

def _magick_cmd(exe):
    # "msvg:" is ImageMagick's own SVG drawing, which works even without librsvg installed
    def cmd(path, out, w, h):
        box = '%sx%s' % (w or '', h or '')
        return [exe, '-background', 'none', '-density', '96', 'msvg:' + path, '-resize', box, 'PNG32:' + out]
    return cmd

def find_svg_renderer():
    """First working SVG -> PNG backend, as a function (path, width, height) -> PNG bytes."""
    try:
        import cairosvg  # noqa: F401
        return 'cairosvg', _via_cairosvg
    except Exception:
        pass
    if shutil.which('rsvg-convert'): return 'rsvg-convert', _via_cli(_rsvg_cmd)
    if shutil.which('inkscape'):     return 'inkscape', _via_cli(_inkscape_cmd)
    for exe in ('magick', 'convert'):
        if shutil.which(exe):        return 'ImageMagick (' + exe + ')', _via_cli(_magick_cmd(exe))
    return None, None

def inline_css(svg_path, out_path):
    """Get an SVG ready for simple drawing tools. Many exporters (Illustrator, Figma...)
    color shapes with CSS classes in a <style> block: .cls-1{fill:#fff}. Simple tools
    ignore those and draw the shapes black or not at all, so the classes' declarations
    are copied onto the shapes as plain attributes. Clip paths are removed too (see
    below). Returns the file to draw (the original if nothing needed doing)."""
    try:
        with open(svg_path, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
        if '<style' not in text and 'clip-path' not in text:
            return svg_path
        for m in re.finditer(r'xmlns:?(\w*)="([^"]+)"', text):
            ET.register_namespace(m.group(1), m.group(2))
        root = ET.fromstring(text.encode('utf-8'))
        rules = {}
        for st in [e for e in root.iter() if e.tag.split('}')[-1] == 'style']:
            css = re.sub(r'/\*.*?\*/', '', st.text or '', flags=re.S)
            for sel, body in re.findall(r'([^{}]+)\{([^}]*)\}', css):
                decls = {}
                for d in body.split(';'):
                    if ':' in d:
                        k, v = d.split(':', 1)
                        decls[k.strip()] = v.strip()
                for one in sel.split(','):
                    one = one.strip()
                    if one.startswith('.'):
                        rules.setdefault(one[1:], {}).update(decls)
        for el in root.iter():
            for cls in (el.get('class') or '').split():
                for k, v in rules.get(cls, {}).items():
                    el.set(k, v)
            # clip-path: the simple renderers draw NOTHING when a shape is clipped, and the
            # clip is nearly always just the picture's own border, so leave it out.
            el.attrib.pop('clip-path', None)
            if 'clip-path' in (el.get('style') or ''):
                el.set('style', re.sub(r'clip-path\s*:[^;]*;?', '', el.get('style')))
        for parent in root.iter():                        # drop the clip definitions
            for child in list(parent):
                if child.tag.split('}')[-1] == 'clipPath':
                    parent.remove(child)
        for parent in root.iter():                        # drop the <style> blocks
            for child in list(parent):
                if child.tag.split('}')[-1] == 'style':
                    parent.remove(child)
        ET.ElementTree(root).write(out_path, encoding='utf-8', xml_declaration=True)
        return out_path
    except Exception:
        return svg_path                                   # unreadable: let the renderer try the original

def render_svg(render, path, size):
    """Draw an SVG into a size x size RGBA picture, keeping its proportions."""
    px = size * SUPERSAMPLE
    with tempfile.TemporaryDirectory() as tmp:
        path = inline_css(path, os.path.join(tmp, 'logo.svg'))
        im = Image.open(io.BytesIO(render(path, px, None))).convert('RGBA')
        if im.height > px:                               # taller than wide: fit the height instead
            im = Image.open(io.BytesIO(render(path, None, px))).convert('RGBA')
    canvas = Image.new('RGBA', (px, px), (0, 0, 0, 0))
    canvas.paste(im, ((px - im.width) // 2, (px - im.height) // 2))
    return canvas

# ---------------------------------------------------------------- color / packing
def parse_bg(text):
    t = text.lower()
    if t == 'white': return (255, 255, 255)
    if t == 'black': return (0, 0, 0)
    t = t.lstrip('#')
    return (int(t[0:2], 16), int(t[2:4], 16), int(t[4:6], 16))

def pack(code):
    """3 characters (0-9, A-Z) -> one number, same rule as LogoDraw.cpp."""
    v = 0
    for ch in code:
        if '0' <= ch <= '9': d = ord(ch) - 48
        elif 'A' <= ch <= 'Z': d = ord(ch) - 65 + 10
        else: raise ValueError(code)
        v = v * 36 + d
    return v

def to565(im, size, bg):
    rgba = im.convert('RGBA')
    flat = Image.new('RGBA', rgba.size, bg + (255,))
    flat.alpha_composite(rgba)
    rgb = flat.convert('RGB')
    if rgb.size != (size, size):
        rgb = rgb.resize((size, size), Image.LANCZOS)
    pixels = rgb.get_flattened_data() if hasattr(rgb, 'get_flattened_data') else rgb.getdata()
    return [(((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)
            for r, g, b in pixels]

def rle(px):
    data = bytearray()
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 255:
            j += 1
        data += bytes((px[i] & 255, px[i] >> 8, j - i))
        i = j
    return bytes(data)

def unrle(data):
    px = []
    for k in range(0, len(data), 3):
        px += [data[k] | (data[k + 1] << 8)] * data[k + 2]
    return px

# ---------------------------------------------------------------- main
def main():
    args, bg_text = [], 'white'
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        if argv[i] == '--bg' and i + 1 < len(argv): bg_text = argv[i + 1]; i += 2
        elif argv[i] in ('-h', '--help'): print(__doc__); return
        else: args.append(argv[i]); i += 1
    if not args:
        print(__doc__); sys.exit(1)

    folder = args[0]
    here = os.path.dirname(os.path.abspath(__file__))
    out = args[1] if len(args) > 1 else os.path.join(here, '..', 'src', 'modules', 'flight', 'LogoData.cpp')
    size = int(args[2]) if len(args) > 2 else DEFAULT_SIZE
    bg = parse_bg(bg_text)

    # one file per code; SVG wins over bitmaps (the bitmap is kept as a fallback)
    chosen, fallback = {}, {}
    for pattern in ('*.bmp', '*.png', '*.svg'):
        for f in sorted(glob.glob(os.path.join(folder, pattern)) + glob.glob(os.path.join(folder, pattern.upper()))):
            code = os.path.splitext(os.path.basename(f))[0].upper()
            if code in chosen and not chosen[code].lower().endswith('.svg'): fallback[code] = chosen[code]
            chosen[code] = f

    renderer_name, render = None, None
    if any(f.lower().endswith('.svg') for f in chosen.values()):
        renderer_name, render = find_svg_renderer()
        if not render:
            print('SVG files found, but nothing to draw them with. Install one of: '
                  'cairosvg (pip3 install cairosvg), rsvg-convert (brew install librsvg), inkscape.')
            sys.exit(2)
        print('drawing SVGs with', renderer_name)

    codes, skipped, n_svg = {}, [], 0
    for code, f in sorted(chosen.items()):
        if len(code) != 3 or not code.isalnum():
            skipped.append((f, 'name is not 3 letters/digits')); continue
        try:
            if f.lower().endswith('.svg'):
                try:
                    im = render_svg(render, f, size); n_svg += 1
                except Exception:
                    if code not in fallback: raise
                    skipped.append((f, 'could not draw the SVG, used %s instead' % os.path.basename(fallback[code])))
                    im = Image.open(fallback[code])
            else:
                im = Image.open(f)
                if im.size[0] != im.size[1]:
                    skipped.append((f, 'not square (%dx%d)' % im.size)); continue
            codes[code] = to565(im, size, bg)
        except Exception as e:                            # a broken file should not stop the rest
            skipped.append((f, 'could not read: %s' % str(e).strip().splitlines()[-1][:80] if str(e).strip() else 'could not read'))

    images, index_of, offsets, blob = [], {}, [], bytearray()
    code_list = sorted(codes, key=pack)
    image_for = []
    for code in code_list:
        key = hashlib.md5(bytes(x for p in codes[code] for x in (p & 255, p >> 8))).hexdigest()
        if key not in index_of:
            enc = rle(codes[code])
            assert unrle(enc) == codes[code], code            # self-check
            index_of[key] = len(images)
            images.append(enc)
            offsets.append(len(blob))
            blob += enc
        image_for.append(index_of[key])
    offsets.append(len(blob))

    def rows(vals, per=16):
        return ',\n'.join('  ' + ','.join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per))

    with open(out, 'w') as o:
        o.write('// GENERATED by tools/make_logos.py - do not edit by hand.\n')
        o.write('// %d airline logos (%d different pictures), %d bytes of picture data.\n' % (len(code_list), len(images), len(blob)))
        o.write('#include "LogoData.h"\n\n')
        o.write('const uint16_t LOGO_COUNT = %d;\nconst uint16_t LOGO_UNIQUE = %d;\nconst uint8_t  LOGO_SIZE = %d;\n\n' % (len(code_list), len(images), size))
        o.write('const uint16_t LOGO_CODES[] = {\n%s\n};\n\n' % rows([pack(c) for c in code_list]))
        o.write('const uint16_t LOGO_IMAGE[] = {\n%s\n};\n\n' % rows(image_for))
        o.write('const uint32_t LOGO_OFFSET[] = {\n%s\n};\n\n' % rows(offsets, 12))
        o.write('const uint8_t LOGO_DATA[] = {\n%s\n};\n' % rows(list(blob), 32))

    print('logos: %d (%d from SVG)  size: %dx%d  distinct pictures: %d  data: %d bytes  -> %s'
          % (len(code_list), n_svg, size, size, len(images), len(blob), os.path.normpath(out)))
    for f, why in skipped:
        print('skipped', os.path.basename(f), '-', why)

main()
