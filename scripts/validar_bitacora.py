"""Comprueba la serie final y su correspondencia con los registros originales."""
import csv,collections,math,re
from pathlib import Path
raiz = Path(__file__).resolve().parents[1]
rows=list(csv.DictReader(open(raiz / 'resultados/bitacora.csv',encoding='utf-8')))
assert len(rows)==550
counts=collections.Counter((r['programa'],r['dificultad'],r['bits'],r['procesos'],r['hilos']) for r in rows)
assert len(counts)==110 and set(counts.values())=={5}
for r in rows:
 assert r['bits']=='22' and math.isfinite(float(r['segundos'])) and float(r['segundos'])>0
 if r['programa']=='secuencial': name=f"secuencial_{r['dificultad']}_{r['repeticion']}"
 elif r['programa']=='hibrido': name=f"hibrido_{r['dificultad']}_{r['procesos']}x{r['hilos']}_{r['repeticion']}"
 else: name=f"{r['programa']}_{r['dificultad']}_{r['procesos']}_{r['repeticion']}"
 s=Path(raiz, 'resultados/ejecuciones',name+'.txt').read_text(encoding='utf-8')
 assert re.search(r'^Llave encontrada: '+r['llave']+'$',s,re.M), name
 assert re.search(r'^Tiempo: '+re.escape(r['segundos'])+' s$',s,re.M), name
 assert 'Espacio de llaves: 2^22' in s,name
print('550 filas válidas, 110 configuraciones con cinco repeticiones y 550 registros coincidentes.')
