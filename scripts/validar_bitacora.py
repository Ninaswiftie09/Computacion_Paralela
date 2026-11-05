"""Comprueba las mediciones y su correspondencia con las salidas originales."""
import argparse
import collections
import csv
import math
from pathlib import Path
import re

raiz = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--csv', type=Path, default=raiz / 'resultados/bitacora.csv')
parser.add_argument('--registros', type=Path, default=raiz / 'resultados/ejecuciones')
args = parser.parse_args()
with args.csv.open(encoding='utf-8', newline='') as archivo:
    filas = list(csv.DictReader(archivo))
assert len(filas) == 110
configuraciones = {('secuencial', '1', '1')}
configuraciones.update((p, str(n), '1') for p in ['naive', 'ciclico', 'permutado', 'dinamico'] for n in [1, 2, 4, 8])
configuraciones.update(('hibrido', str(p), str(h)) for p, h in [(1, 2), (1, 4), (1, 8), (2, 4), (4, 2)])
conteos = collections.Counter((f['programa'], f['procesos'], f['hilos']) for f in filas)
assert set(conteos) == configuraciones and set(conteos.values()) == {5}
repeticiones = collections.defaultdict(set)
for f in filas:
    assert f['bits'] == '24' and f['llave'] == '16700000' and f['dificultad'] == 'dificil'
    assert math.isfinite(float(f['segundos'])) and float(f['segundos']) > 0
    assert int(f['iter_ganador']) > 0
    repeticiones[f['programa'], f['procesos'], f['hilos']].add(int(f['repeticion']))
    if f['programa'] == 'secuencial':
        nombre = f"secuencial_dificil_{f['repeticion']}"
        assert f['iter_ganador'] == '16700001'
    elif f['programa'] == 'hibrido':
        nombre = f"hibrido_dificil_{f['procesos']}x{f['hilos']}_{f['repeticion']}"
    else:
        nombre = f"{f['programa']}_dificil_{f['procesos']}_{f['repeticion']}"
    if f['programa'] == 'permutado':
        assert f['semilla'] == f['repeticion']
    if f['programa'] == 'dinamico':
        assert f['bloque'] == '4096'
    if f['programa'] == 'ciclico':
        assert int(f['iter_ganador']) == 16700000 // int(f['procesos']) + 1
    if f['programa'] == 'naive':
        ancho = (1 << 24) // int(f['procesos'])
        assert int(f['iter_ganador']) == 16700000 % ancho + 1
    salida = (args.registros / (nombre + '.txt')).read_text(encoding='utf-8')
    assert re.search(r'^Llave encontrada: 16700000$', salida, re.M), nombre
    assert re.search(r'^Tiempo: ' + re.escape(f['segundos']) + ' s$', salida, re.M), nombre
    assert 'Espacio de llaves: 2^24' in salida, nombre
    etiqueta = 'Iteraciones' if f['programa'] == 'secuencial' else 'Iteraciones del proceso ganador'
    assert re.search('^' + etiqueta + ': ' + f['iter_ganador'] + '$', salida, re.M), nombre
assert all(r == {1, 2, 3, 4, 5} for r in repeticiones.values())
print('110 filas válidas, 22 configuraciones con cinco repeticiones y 110 registros coincidentes.')
