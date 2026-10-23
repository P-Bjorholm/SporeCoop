#!/usr/bin/env python3
"""The setup's .ico from the co-op menu icon: make_ico.py <in.png> <out.ico>"""
import sys

from PIL import Image

Image.open(sys.argv[1]).convert('RGBA').save(sys.argv[2], sizes=[(16, 16), (24, 24), (32, 32), (48, 48)])
