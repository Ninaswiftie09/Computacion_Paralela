"""Representa las salidas de ejecución como imágenes legibles para el informe."""
from pathlib import Path
import textwrap

from PIL import Image, ImageDraw, ImageFont

raiz = Path(__file__).resolve().parents[1]
destino = raiz / 'docs/informe/capturas'
destino.mkdir(parents=True, exist_ok=True)
fuentes = [Path('C:/Windows/Fonts/consola.ttf'),
           Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')]
fuente = ImageFont.truetype(str(next(p for p in fuentes if p.exists())), 25)
registros = {
    'compilacion': raiz / 'resultados/compilacion.txt',
    'guia': raiz / 'resultados/guia.txt',
    'verificacion': raiz / 'resultados/verificacion.txt',
    'openssl': raiz / 'resultados/openssl.txt',
    'dinamico': raiz / 'resultados/ejecuciones/dinamico_dificil_4_1.txt',
    'entorno': raiz / 'resultados/entorno.txt',
}
for nombre, ruta in registros.items():
    contenido = ruta.read_text(encoding='utf-8', errors='replace').splitlines()
    if nombre == 'guia':
        inicio = next(i for i, l in enumerate(contenido) if l.startswith('Con una llave incorrecta'))
        fin = next(i for i, l in enumerate(contenido) if '===== B.2' in l)
        contenido = contenido[:inicio] + contenido[fin:]
    if nombre == 'verificacion':
        contenido = [contenido[0], contenido[-1]]
    lineas = []
    for linea in contenido:
        limpia = ''.join(c if c.isprintable() or c == '\t' else '?' for c in linea)
        lineas.extend(textwrap.wrap(limpia.expandtabs(4), width=105,
                                   replace_whitespace=False, drop_whitespace=False) or [''])
    ancho = max(1100, max(int(fuente.getlength(l)) for l in lineas) + 64)
    imagen = Image.new('RGB', (ancho, 64 + 34 * len(lineas)), 'white')
    dibujar = ImageDraw.Draw(imagen)
    for i, linea in enumerate(lineas):
        dibujar.text((32, 25 + 34 * i), linea, font=fuente, fill='#17212b')
    imagen.save(destino / (nombre + '.jpg'), quality=96, subsampling=0, dpi=(180, 180))
    print(nombre, len(lineas), imagen.size)
