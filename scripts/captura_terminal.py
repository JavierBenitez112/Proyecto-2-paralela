#!/usr/bin/env python3
"""Convierte la salida real de un comando (archivo .txt) en una imagen PNG
con aspecto de terminal, para usarla como captura en el informe.

Uso:  python3 scripts/captura_terminal.py entrada.txt salida.png ["titulo"]

El archivo de entrada lo genera scripts/bitacora.sh: la primera linea es el
comando ejecutado (precedido de "$ ") y el resto es su salida sin editar.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FUENTES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "C:/Windows/Fonts/consola.ttf",
]
FONDO, TEXTO, PROMPT, BARRA = (24, 26, 33), (220, 223, 228), (126, 211, 131), (52, 56, 66)
TAM, MARGEN, ALTO_BARRA, MAX_COL = 15, 18, 30, 110


def fuente():
    for ruta in FUENTES:
        try:
            return ImageFont.truetype(ruta, TAM)
        except OSError:
            continue
    return ImageFont.load_default()


def partir(linea):
    """Corta lineas largas para que la imagen no quede demasiado ancha."""
    if len(linea) <= MAX_COL:
        return [linea]
    return [linea[i:i + MAX_COL] for i in range(0, len(linea), MAX_COL)]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        crudas = f.read().rstrip("\n").split("\n")
    titulo = sys.argv[3] if len(sys.argv) > 3 else "Terminal"
    lineas = [(p, c.startswith("$ ")) for c in crudas for p in partir(c)]

    f = fuente()
    ancho_car = f.getbbox("M")[2]
    alto_lin = int(TAM * 1.45)
    cols = max(len(l) for l, _ in lineas)
    w = max(640, 2 * MARGEN + cols * ancho_car)
    h = ALTO_BARRA + 2 * MARGEN + alto_lin * len(lineas)

    img = Image.new("RGB", (w, h), FONDO)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w, ALTO_BARRA], fill=BARRA)
    for i, color in enumerate([(237, 106, 94), (245, 191, 79), (98, 197, 84)]):
        x = 14 + i * 20
        d.ellipse([x, 9, x + 12, 21], fill=color)
    d.text((80, 7), titulo, font=f, fill=(190, 194, 202))
    y = ALTO_BARRA + MARGEN
    for linea, es_cmd in lineas:
        d.text((MARGEN, y), linea, font=f, fill=PROMPT if es_cmd else TEXTO)
        y += alto_lin
    img.save(sys.argv[2])


if __name__ == "__main__":
    main()
