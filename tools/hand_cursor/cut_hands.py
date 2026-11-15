"""The hand of god cursor: the four states cut from docs/design/hand_of_god/hand_of_god_concept.png, alpha from the
brightness above the dark background (the faint gold glow stays faint), 128 px tall (drawn at 48 px, DPI-scaled).
Prints each sprite's size and its hotspot (palm centre; the fingertip for POINT) as fractions of the sprite."""
import numpy as np
from PIL import Image

src = np.asarray(Image.open("../../docs/design/hand_of_god/hand_of_god_concept.png").convert("RGB"), dtype=np.float32) / 255.0
boxes = {  # x0, y0, x1, y1 in the 1536 x 1024 concept; hotspot in concept pixels
    "Open": ((70, 160, 330, 590), (205, 360)),
    "Grab": ((430, 170, 655, 590), (540, 330)),
    "Point": ((735, 200, 1030, 595), (1003, 226)),
    "Fling": ((1080, 120, 1415, 595), (1255, 400)),
}
for name, ((x0, y0, x1, y1), (hx, hy)) in boxes.items():
    c = src[y0:y1, x0:x1]
    lum = c.max(axis=2)
    a = np.clip((lum - 0.075) / (0.42 - 0.075), 0.0, 1.0)
    a = a * a * (3 - 2 * a)
    rgba = np.dstack([c, a])
    im = Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA")
    h = 128
    w = int(round(im.width * h / im.height))
    im = im.resize((w, h), Image.LANCZOS)
    im.save("T_Hand_%s.png" % name)
    print(name, w, h, "hotspot %.3f %.3f" % ((hx - x0) / (x1 - x0), (hy - y0) / (y1 - y0)))
