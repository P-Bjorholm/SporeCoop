#!/usr/bin/env python3
"""Draw the co-op UI icons (white shapes, the game tints them): data/ui/*.png"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), 'data', 'ui')
SS = 8  # supersampling


def person(d, cx, head_y, r, body_w, body_top, body_bottom, s, grow=0):
    g = grow
    d.ellipse([(cx - r - g) * s, (head_y - r - g) * s, (cx + r + g) * s, (head_y + r + g) * s], fill=255)
    d.rounded_rectangle([(cx - body_w - g) * s, (body_top - g) * s, (cx + body_w + g) * s, (body_bottom + g) * s],
                        radius=(body_w * 0.9 + g) * s, fill=255)


def coop_icon(size=62):
    s = SS
    mask = Image.new('L', (size * s, size * s), 0)
    d = ImageDraw.Draw(mask)
    person(d, 41, 15, 7, 11, 25, 46, s)                 # friend (back)
    front = (23, 22, 8.5, 14, 34, 58)
    person(d, *front, s, grow=3.5)                      # gap around the player...
    d2 = ImageDraw.Draw(mask)
    cut = Image.new('L', mask.size, 0)
    person(ImageDraw.Draw(cut), *front, s, grow=3.5)
    mask.paste(0, (0, 0), cut)
    person(d2, *front, s)                               # ...and the player (front)
    mask = mask.resize((size, size), Image.LANCZOS)
    img = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    img.putalpha(mask)
    return img


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    coop_icon().save(os.path.join(OUT, 'coop_icon.png'))
    print('wrote', os.path.join(OUT, 'coop_icon.png'))
