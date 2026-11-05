"""Actualiza las interpretaciones del informe con las mediciones completas."""
from pathlib import Path
import re

import pandas as pd

raiz = Path(__file__).resolve().parents[1]
datos = pd.read_csv(raiz / 'resultados/bitacora.csv')
assert len(datos) == 110 and set(datos.llave) == {16700000} and set(datos.bits) == {24}
res = datos.groupby(['programa', 'procesos', 'hilos']).segundos.agg(['mean', 'std', 'size'])
assert len(res) == 22 and set(res['size']) == {5}
base = res.loc[('secuencial', 1, 1), 'mean']
res['speedup'] = base / res['mean']
res['cv'] = 100 * res['std'] / res['mean']
nombres = {'naive': 'naive', 'ciclico': 'cíclico', 'permutado': 'permutado',
           'dinamico': 'dinámico', 'hibrido': 'híbrido', 'secuencial': 'secuencial'}
ruta = raiz / 'docs/informe/informe.tex'
texto = ruta.read_text(encoding='utf-8')

def reemplazar_desde_hasta(inicio, fin, contenido):
    global texto
    a = texto.index(inicio)
    b = texto.index(fin, a)
    texto = texto[:a] + contenido + '\n\n' + texto[b:]

def tiempo(algoritmo, procesos=4, hilos=1):
    return res.loc[(algoritmo, procesos, hilos), 'mean']

def sp(algoritmo, procesos=4, hilos=1):
    return res.loc[(algoritmo, procesos, hilos), 'speedup']

seccion = texto.index(r'\subsection{Comparación con cuatro procesos}')
fin_tabla = texto.index(r'\end{table}', seccion) + len(r'\end{table}')
inicio_figura = texto.index(r'\begin{figure}', fin_tabla)
parrafos = (
    f'El secuencial obtuvo un promedio de {base:.4f} segundos. Con cuatro procesos, '
    f'naive tardó {tiempo("naive"):.4f} segundos, el cíclico {tiempo("ciclico"):.4f}, '
    f'el permutado {tiempo("permutado"):.4f} y el dinámico {tiempo("dinamico"):.4f}. '
    f'Sus speedups fueron {sp("naive"):.2f}, {sp("ciclico"):.2f}, '
    f'{sp("permutado"):.2f} y {sp("dinamico"):.2f}, respectivamente.\n\n'
    'En naive, la llave pertenece al proceso 3 y está a 4117088 posiciones del inicio '
    'de su rango. El ganador realiza 4117089 intentos. En el cíclico realiza '
    '4175001 intentos. Esta diferencia muestra que el speedup depende tanto '
    'del reparto como del trabajo necesario para llegar a la solución.\n\n'
    'El dinámico dispone de tres trabajadores y un coordinador. Su tiempo incluye '
    'los pedidos y la entrega de bloques de 4096 llaves. En permutado, la desviación '
    'también refleja que las cinco semillas colocan la solución en posiciones distintas. '
    'Una llave alta no garantiza que el secuencial sea el más lento en todas las configuraciones.'
)
texto = texto[:fin_tabla] + '\n\n' + parrafos + '\n\n' + texto[inicio_figura:]

ganadores = []
for w in [2, 4, 8]:
    opciones = [(a, p, h) for a, p, h in res.index if p * h == w]
    ganador = min(opciones, key=lambda k: res.loc[k, 'mean'])
    a, p, h = ganador
    ganadores.append(f'con {w} recursos lógicos ganó {nombres[a]} '
                     f'({p} procesos y {h} {"hilo" if h == 1 else "hilos"} por proceso), con '
                     f'{res.loc[ganador, "mean"]:.4f} segundos y speedup '
                     f'{res.loc[ganador, "speedup"]:.2f}')
parrafo = ('La línea ideal representa un speedup proporcional a la cantidad de procesos. '
           'Los mensajes, el coordinador y los recursos compartidos pueden alejar los resultados '
           'de esa referencia. Con igual total de recursos lógicos se obtuvieron estos ganadores. '
           + '. '.join(g[0].upper() + g[1:] for g in ganadores) + '. Las diferencias pequeñas se interpretan junto con la desviación estándar.')
reemplazar_desde_hasta('La línea ideal ', r'\subsection{MPI con OpenMP}', parrafo)

hib = res.xs('hibrido', level='programa')
ocho = hib.loc[[k for k in hib.index if k[0] * k[1] == 8], 'mean']
mejor = ocho.idxmin()
ventaja = 'menor' if ocho.min() < tiempo('ciclico', 8) else 'mayor'
parrafo = (
    f'El cíclico con ocho procesos obtuvo {tiempo("ciclico", 8):.4f} segundos y '
    f'speedup {sp("ciclico", 8):.2f}. Las tres configuraciones híbridas con ocho '
    f'recursos lógicos registraron promedios entre {ocho.min():.4f} y {ocho.max():.4f} segundos. '
    f'La más rápida usó {mejor[0]} procesos y {mejor[1]} hilos por proceso, '
    f'con un tiempo {ventaja} que el cíclico de ocho procesos.\n\n'
    'Una mejora frente al secuencial no demuestra por sí sola una ventaja de OpenMP. '
    'La comparación con MPI puro usa el mismo total de recursos lógicos. '
    'El resultado se limita a la llave, el equipo y las combinaciones medidas.'
)
seccion = texto.index(r'\subsection{MPI con OpenMP}')
inicio_parrafo = texto.index(r'\end{figure}', seccion) + len(r'\end{figure}')
fin_parrafo = texto.index(r'\section{Conclusiones}', inicio_parrafo)
texto = texto[:inicio_parrafo] + '\n\n' + parrafo + '\n\n' + texto[fin_parrafo:]

paralelos = res.drop(index='secuencial', level='programa')
rapido = paralelos['mean'].idxmin()
estables = paralelos.loc[[k for k in paralelos.index if k[1] * k[2] > 1]]
estable = estables['cv'].idxmin()
a, p, h = rapido
e, ep, eh = estable
conclusiones = (
    r'\section{Conclusiones}' + '\n'
    f'Para la llave 16700000, el secuencial realizó 16700001 intentos y tardó '
    f'{base:.4f} segundos en promedio. La configuración más rápida fue '
    f'{nombres[a]} con {p} procesos y {h} {"hilo" if h == 1 else "hilos"} por proceso: '
    f'{res.loc[rapido, "mean"]:.4f} segundos y speedup {res.loc[rapido, "speedup"]:.2f}.\n\n'
    'El reparto naive reduce el trabajo hasta la solución cuando la llave queda cerca '
    'del inicio de un rango. El cíclico distribuye el avance por saltos y el permutado '
    'cambia el orden de búsqueda. Por ello, el valor de la llave no basta para anticipar '
    'qué algoritmo será el más rápido.\n\n'
    'El dinámico reparte bloques según los pedidos de los trabajadores, con el costo '
    'de los mensajes y de reservar un proceso para coordinar. El híbrido combina '
    'procesos e hilos, cuya ventaja debe evaluarse frente a MPI puro con iguales recursos.\n\n'
    f'Entre las configuraciones con más de un recurso lógico, la menor variación relativa '
    f'entre repeticiones correspondió a {nombres[e]} con {ep} procesos y {eh} {"hilo" if eh == 1 else "hilos"}: '
    f'$100s_T/\\overline{{T}}={res.loc[estable, "cv"]:.2f}\\%$. '
    'Este resultado describe la repetición de una sola llave y no permite concluir '
    'estabilidad entre distintas llaves.\n\n'
    'Se recuperó la llave esperada en las 110 ejecuciones. La coordinación de mensajes '
    'y los datos separados por hilo permiten cerrar la búsqueda sin dejar comunicaciones '
    'pendientes. Los tiempos corresponden a un espacio limitado de 24 bits, no al '
    'recorrido completo de DES de 56 bits.'
)
reemplazar_desde_hasta(r'\section{Conclusiones}', r'\section{Recomendaciones}', conclusiones)

entorno = (raiz / 'resultados/entorno.txt').read_text(encoding='utf-8')
version = re.search(r'OpenSSL ([\d.]+)', entorno).group(1)
texto = texto.replace('GCC 14.2.0 con O2 y Open MPI 5.0.7.',
                      f'GCC 14.2.0 con O2, Open MPI 5.0.7 y OpenSSL {version}.')
ruta.write_text(texto, encoding='utf-8')
print(f'Secuencial: {base:.6f} s')
print(res[['mean', 'std', 'speedup', 'cv']].to_string())
print('Ganador:', rapido)
print('Menor variación relativa:', estable)
