"""Banner largo do README (assets/banner.jpg), a partir da capa quadrada da Workshop.

    python steam/gerar-banner.py steam/preview1.png assets/banner.jpg

Fundo: a capa esticada, desfocada e escurecida. À direita, o samurai com a medalha (recorte nítido,
fundindo no fundo pela esquerda). À esquerda, o título no mesmo estilo da capa.
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
W, H = 1280, 400

art = Image.open(SRC).convert("RGB")  # 1024x1024, com o título no topo e a legenda embaixo

# Recorte: a cena inteira sem o título da capa (y >= 165) e sem a legenda (y <= 905)
crop = art.crop((0, 165, 1024, 905))
crop = crop.resize((int(crop.width * H / crop.height), H), Image.LANCZOS)

# Fundo: degradê vertical com as cores médias da própria cena (céu em cima, areia embaixo), escurecido
bg = crop.resize((1, H), Image.LANCZOS).resize((W, H))
bg = Image.blend(bg, Image.new("RGB", (W, H), (22, 15, 8)), 0.6)

# Cena à direita, fundindo no fundo pela esquerda
x0 = W - crop.width
mask = Image.new("L", crop.size, 255)
px = mask.load()
fade = 300  # pixels de transição na borda esquerda
for x in range(fade):
    a = int(255 * (x / fade) ** 1.6)
    for y in range(crop.height):
        px[x, y] = a
bg.paste(crop, (x0, 0), mask)


def font(size, variation):
    f = ImageFont.truetype(r"C:\Windows\Fonts\bahnschrift.ttf", size)
    try:
        f.set_variation_by_name(variation)
    except Exception:
        pass
    return f


def spaced(draw, x, y, text, f, fill, spacing):
    # sombra desfocada, depois o texto
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    xx = x
    for ch in text:
        ld.text((xx + 3, y + 4), ch, font=f, fill=(0, 0, 0, 210))
        xx += draw.textlength(ch, font=f) + spacing
    layer = layer.filter(ImageFilter.GaussianBlur(4))
    bg.paste(layer, (0, 0), layer)
    d = ImageDraw.Draw(bg)
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + spacing


BONE = (238, 226, 204)
GOLD = (236, 184, 72)
d = ImageDraw.Draw(bg)
LEFT = 72
spaced(d, LEFT + 4, 92, "KENSHI", font(44, "Bold SemiCondensed"), BONE, 16)
spaced(d, LEFT, 140, "ACHIEVEMENTS", font(96, "Bold Condensed"), GOLD, 5)
spaced(d, LEFT + 4, 262, "KILL COUNT  \u2022  KNOCKOUTS  \u2022  68 ACHIEVEMENTS", font(28, "SemiBold SemiCondensed"), BONE, 2)
spaced(d, LEFT + 4, 304, "An RE_Kenshi plugin", font(24, "Regular SemiCondensed"), (200, 186, 160), 1)

bg.save(OUT, "JPEG", quality=88, optimize=True)
print(OUT, bg.size)
