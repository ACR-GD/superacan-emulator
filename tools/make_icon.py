#!/usr/bin/env python3
"""Generate the app icon (console gray + A'Can green) as an .icns."""
import os, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = 1024
def font(size):
    for p in ["/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc",
              "/Library/Fonts/Arial Bold.ttf"]:
        try:
            f = ImageFont.truetype(p, size)
            try: f.set_variation_by_name("Black")
            except Exception: pass
            return f
        except Exception:
            continue
    return ImageFont.load_default()

def vgrad(w, h, top, bot):
    g = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        g.putpixel((0, y), tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
    return g.resize((w, h))

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
m = 100                                     # Apple icon grid margin
body = (m, m, S - m, S - m)
# drop shadow
sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(sh).rounded_rectangle((m, m + 18, S - m, S - m + 18), 185, fill=(0, 0, 0, 110))
img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(22)))
# console-gray shell
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle(body, 185, fill=255)
img.paste(vgrad(S, S, (206, 210, 213), (160, 166, 170)), (0, 0), mask)
d = ImageDraw.Draw(img)
# grip ridges
for i in range(3):
    y = 205 + i * 34
    d.rounded_rectangle((300, y, S - 300, y + 14), 7, fill=(140, 146, 150))
# green label
lab = (205, 335, S - 205, S - 205)
lmask = Image.new("L", (S, S), 0)
ImageDraw.Draw(lmask).rounded_rectangle(lab, 46, fill=255)
img.paste(vgrad(S, S, (84, 210, 104), (30, 122, 52)), (0, 0), lmask)
d.rectangle((lab[0], lab[1] + 70, lab[2], lab[1] + 92), fill=(255, 255, 255, 46))
# A' mark
f = font(330)
txt = "A'"
bb = d.textbbox((0, 0), txt, font=f)
tw, th = bb[2] - bb[0], bb[3] - bb[1]
cx, cy = (lab[0] + lab[2]) / 2, (lab[1] + lab[3]) / 2 + 30
d.text((cx - tw / 2 - bb[0] + 6, cy - th / 2 - bb[1] + 6), txt, font=f, fill=(0, 60, 20, 90))
d.text((cx - tw / 2 - bb[0], cy - th / 2 - bb[1]), txt, font=f, fill=(255, 255, 255, 255))
# power LED
d.ellipse((S - 268, 196, S - 222, 242), fill=(60, 200, 90))
d.ellipse((S - 258, 204, S - 240, 222), fill=(190, 255, 200))

out = sys.argv[1] if len(sys.argv) > 1 else "AppIcon.icns"
with tempfile.TemporaryDirectory() as tmp:
    iconset = os.path.join(tmp, "AppIcon.iconset")
    os.mkdir(iconset)
    for sz in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = sz * scale
            name = f"icon_{sz}x{sz}{'@2x' if scale == 2 else ''}.png"
            img.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, name))
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", out])
img.save(os.path.splitext(out)[0] + ".png")
print("wrote", out)
