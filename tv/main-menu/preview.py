#!/usr/bin/env python3
"""Prévia local do layout compartilhado (não é captura da TV)."""
import base64
import json
from pathlib import Path
from xml.sax.saxutils import escape
from PIL import ImageFont

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent
items = json.loads((HERE / "assets/layout.json").read_text())["items"]

def image(asset, x, y, w, h, crop=False):
    folder, name = asset.split("/", 1)
    source = BUILD / folder / "assets" / name
    data = base64.b64encode(source.read_bytes()).decode()
    return f'<image x="{x}" y="{y}" width="{w}" height="{h}" preserveAspectRatio="{"xMidYMid slice" if crop else "none"}" xlink:href="data:image/png;base64,{data}"/>'

def text(value, x, y, size, color="#f2eee4", max_width=None):
    font = ImageFont.truetype(str(BUILD / 'character-menu/assets/comic.ttf'), size)
    if max_width and font.getlength(value) > max_width:
        size *= max_width / font.getlength(value)
    return f'<text x="{x}" y="{y}" font-family="CCAdamKubert" font-size="{size}" fill="{color}">{escape(value)}</text>'

svg = ['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1280" height="720" viewBox="0 0 1280 720">']
svg += [image("character-menu/paper.png", 0, 0, 1280, 720), '<path fill="#151211" d="M64 38H1216V134H64Z"/>', text("BLACK OPS ZOMBIES",86,80,30),text("REIMAGINED",85,119,42),text("MAIN MENU",942,100,33)]
svg.append('<rect x="48" y="134" width="1184" height="540" fill="#151211"/>')
for i,t in enumerate(items):
    x,y,w,h=t['bounds']; path=f'M{x} {y+6}L{x+w} {y}L{x+w-3} {y+h-6}L{x+2} {y+h}Z'
    svg += [f'<defs><clipPath id="p{i}"><path d="{path}"/></clipPath></defs><g clip-path="url(#p{i})">',f'<path d="{path}" fill="#151211"/>']
    if 'art' in t: svg.append(image(t['art'],x,y,w,h-100,True))
    if t['caption']:
        top=y+h-112; color='#791d24' if t['id']=='single' else '#151211'
        svg += [f'<rect x="{x}" y="{top}" width="{w}" height="112" fill="{color}"/>',text(t['title'],x+16,top+44,38,max_width=w-32),text(t['caption'],x+16,top+85,19,'#d5c9ac',w-32)]
    else: svg.append(text(t['title'],x+22,y+49,31))
    svg += ['</g>',f'<path d="{path}" fill="none" stroke="#151211" stroke-width="5"/>',f'<path d="{path}" fill="none" stroke="#827763" stroke-width="1.5"/>']
svg += [text('D-PAD: NAVIGATE     A: SELECT     B: BACK',330,698,20,'#151211'),'</svg>']
(HERE/'preview.svg').write_text('\n'.join(svg))
print(HERE/'preview.svg')
