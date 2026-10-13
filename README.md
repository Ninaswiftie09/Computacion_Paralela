# Proyecto 2 – Parte 1 (Persona 1): DES, versión secuencial y acercamiento cíclico

## Requisitos (Ubuntu / Pop!_OS)
```bash
sudo apt install build-essential libssl-dev openmpi-bin libopenmpi-dev
```

## Compilar
```bash
make
```

## Uso
```bash
# 1) Cifrar un .txt con una llave de 56 bits (0 <= llave < 2^56)
./des_tool enc texto.txt cifrado.bin 42
# 2) Descifrar (verificación)
./des_tool dec cifrado.bin 42
# 3) Fuerza bruta secuencial; el 3er argumento limita el espacio a 2^bits llaves
./secuencial cifrado.bin "es una prueba de" 24
# 4) Fuerza bruta MPI, distribución cíclica
mpirun -np 4 ./ciclico cifrado.bin "es una prueba de" 24
# 5) Bitácora automática: llaves fácil/media/difícil escaladas a 2^N
./pruebas.sh 22 4
```

## Archivos
| Archivo | Descripción |
|---|---|
| `des_lib.h/.c` | Interfaz común DES (encrypt, decrypt, try_key) sobre OpenSSL; reemplaza `rpc/des_crypt.h` |
| `des_tool.c` | Cifra/descifra archivos .txt con llave arbitraria |
| `secuencial.c` | Búsqueda secuencial con medición de tiempo |
| `ciclico.c` | Acercamiento alternativo 1 con MPI: llaves r, r+p, r+2p, … |
| `pruebas.sh` | Bitácora de pruebas (secuencial vs cíclico) |

## Convenciones para integrar con el equipo
- Llave = `uint64_t` de 56 bits; buffers cifrados in place en bloques de 8 bytes; relleno con ceros.
- Éxito = el texto descifrado contiene la frase clave (`strstr`).
- Si usan otra librería DES, solo cambia `des_lib.c`; el resto del código no se toca.

## Nota sobre llaves de 2^56
Las llaves de la guía ((2^56)/4, etc.) no terminan en tiempo razonable (2^54 intentos a ~2 M llaves/s por núcleo ≈ siglos).
Por eso `secuencial`/`ciclico` aceptan `bits` para escalar el espacio y comparar las llaves fácil/media/difícil de forma proporcional.
