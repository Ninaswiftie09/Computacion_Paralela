# Proyecto 2 — Computación Paralela y Distribuida
## Fuerza bruta DES con OpenMPI

Este repo contiene un **avance** del proyecto: la base funcional en C con
OpenMPI, lista para compilar y correr. **No** incluye todavía el reporte
escrito completo, el diagrama de flujo de DES, la bitácora formal de
pruebas con capturas, ni el cluster multi-máquina / OpenMP (extras). Ver
la sección "Qué falta" al final.

## Requisitos

- `gcc` (o cualquier compilador C compatible)
- OpenMPI (`mpicc`, `mpirun`)
- OpenSSL dev (`libssl-dev`) — reemplaza `rpc/des_crypt.h`, que ya no
  existe en Linux moderno (se explica en `src/des_lib.c`)

En Ubuntu/Debian:
```bash
sudo apt-get install libopenmpi-dev openmpi-bin libssl-dev
```

## Compilar

```bash
make
```

Genera tres binarios en `bin/`:
- `secuencial` — cifrado/descifrado + búsqueda secuencial de la llave
- `paralelo_naive` — búsqueda paralela, bloques contiguos (acercamiento base)
- `paralelo_intercalado` — Alternativa #1: recorrido intercalado (round-robin)

## Uso

### Cifrar / descifrar (Parte B.1)
```bash
./bin/secuencial cifrar   textos/plano.txt 42 textos/cifrado.bin
./bin/secuencial descifrar textos/cifrado.bin 42 textos/descifrado.txt
```

### Búsqueda secuencial (Parte A.3)
```bash
./bin/secuencial buscar textos/cifrado.bin "es una prueba de" 1000
```
El último argumento es la llave máxima del rango de búsqueda (por
ejemplo `1000` para pruebas rápidas, o `72057594037927935` = 2^56-1
para el espacio completo de 56 bits — ojo, esto último puede no
terminar nunca si la llave real es alta, tal como advierte el
enunciado).

### Búsqueda paralela (Parte B.2 y B.3), con 4 procesos
```bash
mpirun -np 4 ./bin/paralelo_naive       textos/cifrado.bin "es una prueba de" 999
mpirun -np 4 ./bin/paralelo_intercalado textos/cifrado.bin "es una prueba de" 999
```
(Si `mpirun` se queja de "not enough slots" en una máquina con pocos
núcleos, agregar `--oversubscribe`.)

Las llaves de prueba sugeridas por el enunciado (asumiendo 4 procesos):
- Fácil: `(2^56)/2 + 1`
- Medianamente difícil: `(2^56)/2 + (2^56)/8`
- Difícil: `(2^56)/7 + (2^56)/13` (redondeado hacia arriba)
- Casos especiales de la Parte B.2: `123456`, `(2^56)/4`, `(2^56)/4 + 1`

## Qué se probó ya

- Cifrado/descifrado funcionando (round-trip verificado con el texto
  `"Esta es una prueba de proyecto 2"` y llave `42`).
- Búsqueda secuencial: encuentra la llave correcta y mide tiempo.
- `paralelo_naive` y `paralelo_intercalado`: verificados con llave
  "fácil" (42, cae casi al inicio) y llave "difícil" (999, cae al final
  del último bloque con `np=4` y rango `[0,999]`) — se reprodujo el
  fenómeno que describe el enunciado: con la llave al final del rango,
  el tiempo paralelo es prácticamente igual al secuencial (speedup≈1),
  mientras que con la llave fácil el proceso 0 la encuentra casi de
  inmediato.

## Cómo funciona la comunicación MPI (Parte A.6)

Ver los comentarios extensos al inicio de `src/paralelo_naive.c`. En
resumen: cada proceso posta un `MPI_Irecv` no bloqueante escuchando de
`MPI_ANY_SOURCE`; en cada iteración de su búsqueda hace un `MPI_Test`
sobre esa solicitud para ver si otro proceso ya avisó que encontró la
llave; si la encuentra él mismo, hace `MPI_Send` a todos los demás
procesos y cancela (`MPI_Cancel` + `MPI_Wait`) su propio `Irecv`
pendiente; si no la encuentra y llega al final de su bloque sin aviso,
se queda bloqueado en `MPI_Wait` hasta que otro proceso le notifique.

## Qué falta (para completar el proyecto)

1. **Parte A.1-A.2**: investigación de DES + diagrama de flujo del
   algoritmo (no técnico/código, es redacción).
2. **Parte A.5**: explicar con diagramas/dibujos cómo funcionan
   `decrypt`, `encrypt`, `tryKey`, `memcpy`, `strstr` (el código ya las
   implementa en `des_lib.c`, pero la explicación ilustrada para el
   reporte falta).
3. **Parte B.2**: correr y documentar con capturas las pruebas pedidas
   con las llaves `123456`, `(2^56)/4`, `(2^56)/4 + 1` (con `-np 4`).
4. **Parte B.3-B.5**: ya hay 2 acercamientos alternativos (naive +
   intercalado), pero falta la tabla/gráficas comparativas de speedup
   con llaves fácil/media/difícil, y sería bueno un tercer approach
   para el 5% extra.
5. **Reporte escrito completo** según la guía de informes UVG (carátula,
   índice, citas, conclusiones, bibliografía, Anexo 1 y Anexo 2).
6. **Extras opcionales**: cluster MPI en 2+ máquinas (20%), integración
   con OpenMP (10%).
