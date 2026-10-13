# Proyecto 2: búsqueda de llave DES con MPI

Computación Paralela y Distribuida, UVG, Sección 20.

| Integrante | Carné |
|---|---|
| Roberto Barreda | 23354 |
| Nina Najera | 231088 |
| Jose Anton | 221041 |

## Requisitos

Linux o WSL con GCC, Make, Open MPI y OpenSSL.

```bash
sudo apt install build-essential openmpi-bin libopenmpi-dev libssl-dev
```

## Compilar

```bash
make
```

## Ejecutar

```bash
./bin/cifrador cifrar textos/prueba.txt textos/prueba.bin 42
./bin/cifrador descifrar textos/prueba.bin 42
./bin/secuencial textos/prueba.bin "es una prueba de" 24
mpirun -np 4 ./bin/naive textos/prueba.bin "es una prueba de" 24
mpirun -np 4 ./bin/ciclico textos/prueba.bin "es una prueba de" 24
```

El último argumento es opcional. Limita la búsqueda a las llaves menores que 2^bits. Sin él se recorre el espacio completo de 2^56.

| Programa | Qué hace |
|---|---|
| `cifrador` | Cifra un archivo de texto con una llave, o descifra y muestra el texto |
| `secuencial` | Busca la llave en un solo proceso |
| `naive` | Busca la llave con MPI. Cada proceso recorre un rango contiguo |
| `ciclico` | Busca la llave con MPI. El proceso r prueba las llaves r, r+P, r+2P |
