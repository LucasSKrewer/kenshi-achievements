"""Capa da Steam Workshop: arte gerada + título e subtítulo."""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SRC = sys.argv[1]
OUT = sys.argv[2]
SIZE = 1024

im = Image.open(SRC).convert("RGB")
w, h = im.size
s = min(w, h)
im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s)).resize((SIZE, SIZE), Image.LANCZOS)


def font(size, weight="Bold", width="Condensed"):
    f = ImageFont.truetype(r"C:\Windows\Fonts\bahnschrift.ttf", size)
    try:
        f.set_variation_by_name(f"{weight} {width}" if width else weight)
    except Exception:
        try:
            f.set_variation_by_name(weight)
        except Exception:
            pass
    return f


def gradient(img, top, height, max_alpha, from_top):
    """Faixa escura em degradê pra dar leitura ao texto."""
    band = Image.new("L", (SIZE, height))
    px = band.load()
    for y in range(height):
        t = y / (height - 1)
        a = max_alpha * ((1 - t) if from_top else t) ** 1.6
        for x in range(SIZE):
            px[x, y] = int(a)
    shade = Image.new("RGB", (SIZE, height), (18, 12, 6))
    img.paste(shade, (0, top), band)


def spaced_width(draw, text, f, spacing):
    return sum(draw.textlength(ch, font=f) for ch in text) + spacing * (len(text) - 1)


def draw_spaced(img, cx, y, text, f, fill, spacing, shadow=True):
    d = ImageDraw.Draw(img)
    x = cx - spaced_width(d, text, f, spacing) / 2
    if shadow:
        # sombra suave
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer)
        xx = x
        for ch in text:
            ld.text((xx + 3, y + 4), ch, font=f, fill=(0, 0, 0, 200))
            xx += d.textlength(ch, font=f) + spacing
        layer = layer.filter(ImageFilter.GaussianBlur(4))
        img.paste(layer, (0, 0), layer)
        d = ImageDraw.Draw(img)
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + spacing


gradient(im, 0, 230, 190, from_top=True)
gradient(im, SIZE - 190, 190, 215, from_top=False)

BONE = (238, 226, 204)
GOLD = (236, 184, 72)

# Título: "KENSHI" menor e espaçado, "ACHIEVEMENTS" grande em dourado (casa com a medalha)
draw_spaced(im, SIZE / 2, 22, "KENSHI", font(46, "Bold", "SemiCondensed"), BONE, 18)
draw_spaced(im, SIZE / 2, 66, "ACHIEVEMENTS", font(92, "Bold", "Condensed"), GOLD, 6)

# Subtítulo na faixa de baixo
sub = font(38, "SemiBold", "SemiCondensed")
draw_spaced(im, SIZE / 2, SIZE - 78, "KILL COUNT  \u2022  KNOCKOUTS  \u2022  STATS PANEL", sub, BONE, 3)

im.save(OUT, "JPEG", quality=90, optimize=True)
print(OUT, im.size)
