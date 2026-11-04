#!/usr/bin/env python3
"""Screenshots of the desktop simulator: compare them with docs/ or update docs/.

The desktop simulator (examples/desktop-simulator) renders every page of the
GUI with a simulated watch when the environment variable TTG_SCREENSHOTS is
set. The rendering is deterministic, so the images must match docs/ pixel by
pixel:

    tools/screenshots.py --simulator build/examples/desktop-simulator
    tools/screenshots.py --simulator build/examples/desktop-simulator --update

Without --update it exits with 1 when a page differs from docs/ (also run by
ctest as "screenshots"). With --update it writes the pages that changed and
the overview docs/screenshots.png of the README. Needs Pillow; exits with 77
(skipped in ctest) without it.
"""
import argparse
import os
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageChops
except ImportError:
    print("Pillow is not installed: skipped")
    sys.exit(77)

# all pages that the simulator saves
PAGES = ["live", "graph", "trends", "settings", "wifi", "live-light"]
# the pages in docs/screenshots.png (README), left to right
OVERVIEW = ["live", "graph", "trends", "settings", "live-light"]
OVERVIEW_GAP = 10


def render(simulator, out_dir):
    env = dict(os.environ)
    env["TTG_SCREENSHOTS"] = out_dir
    env.setdefault("SDL_VIDEODRIVER", "dummy")  # no window needed
    subprocess.run([simulator], env=env, check=True, timeout=300, stdout=subprocess.DEVNULL)
    return {name: Image.open(os.path.join(out_dir, name + ".bmp")).convert("RGB") for name in PAGES}


def difference(a, b):
    """None if equal, otherwise a description of the difference."""
    if a.size != b.size:
        return "size %s instead of %s" % (b.size, a.size)
    box = ImageChops.difference(a, b).getbbox()
    return None if box is None else "differs in %s" % (box,)


def overview(images):
    w, h = images[OVERVIEW[0]].size
    out = Image.new("RGB", (len(OVERVIEW) * (w + OVERVIEW_GAP) - OVERVIEW_GAP, h), "white")
    for i, name in enumerate(OVERVIEW):
        out.paste(images[name], (i * (w + OVERVIEW_GAP), 0))
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--simulator", required=True, help="the desktop-simulator executable")
    parser.add_argument("--docs", default=os.path.join(root, "docs"), help="the reference images")
    parser.add_argument("--out", help="keep the rendered images in this directory")
    parser.add_argument("--update", action="store_true", help="write the changed images to --docs")
    args = parser.parse_args()

    out_dir = args.out or tempfile.mkdtemp(prefix="ttg-screenshots-")
    os.makedirs(out_dir, exist_ok=True)
    images = render(args.simulator, out_dir)

    changed = []
    for name in PAGES:
        path = os.path.join(args.docs, name + ".png")
        reference = Image.open(path).convert("RGB") if os.path.exists(path) else None
        diff = "missing" if reference is None else difference(reference, images[name])
        if diff is None:
            continue
        changed.append(name)
        print("%s: %s" % (name, diff))
        if args.update:
            images[name].save(path, optimize=True)

    if args.update:
        path = os.path.join(args.docs, "screenshots.png")
        if any(name in OVERVIEW for name in changed) or not os.path.exists(path):
            overview(images).save(path, optimize=True)
            print("screenshots: updated")
        print("%d page(s) updated" % len(changed))
        return 0
    if changed:
        print("Rendered images: %s - update docs/ with --update if the change is intended" % out_dir)
        return 1
    print("all %d pages match docs/" % len(PAGES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
