# CC3069 – Proyecto 02: búsqueda de claves AES-128 con Open MPI

Universidad del Valle de Guatemala · Computación Paralela y Distribuida · Ciclo 2, 2026
Javier Benítez (23405) · Javier López (23415) · Pedro Ávila (23089)

## Programas del Proyecto 02

| Programa | Inciso | Qué hace |
|---|---|---|
| `bin/aes_texto` | 1 | Cifra y descifra un `.txt` con AES-128-CBC y una clave por parámetro |
| `bin/busqueda_secuencial` | 2 | Fuerza bruta con un proceso (referencia para el Speedup) |
| `bin/busqueda_mpi` | 2–4 | Fuerza bruta con Open MPI, estrategia 1 (intercalada, base) o 2 (bloques, alternativa) |

La clave numérica se convierte a 128 bits en big-endian con ceros a la izquierda
(`123456 → 0000000000000000000000000001e240`). El archivo cifrado guarda
`IV (16 B) || AES-128-CBC(texto, PKCS#7)`. Para cada candidata se descifra el
mensaje completo y se busca la frase en cualquier posición; si aparece, se
confirma con el relleno PKCS#7.

### Inciso 1: cifrar y descifrar

```bash
./bin/aes_texto -e 123456 -i datos/prueba.txt -o cifrado.bin      # cifrar
./bin/aes_texto -d 123456 -i cifrado.bin -o recuperado.txt        # descifrar
./bin/aes_texto -r 123456 -i datos/prueba.txt                     # ida y vuelta
```

La clave puede ser decimal, `0x…` o `hex:<32 dígitos>` (clave completa de 16 bytes).

### Incisos 2 y 3: búsqueda con 4 procesos

```bash
# a) 123456, b) 2^56/4, c) 2^56/4 + 1 ; -e 1 = intercalada, -e 2 = bloques
mpirun -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k 123456            -e 1
mpirun -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k 18014398509481984 -e 2
mpirun -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k 18014398509481985 -e 1 -t 60

./bin/busqueda_secuencial -i datos/prueba.txt -k 123456
```

Con `-i texto -k CLAVE` el programa cifra el texto con CLAVE solo para preparar
la prueba; la búsqueda no la usa (únicamente se reporta para estimar el tiempo
si `-t` corta la búsqueda). También se puede buscar sobre un archivo de
`aes_texto`: `mpirun -np 4 ./bin/busqueda_mpi -c cifrado.bin -e 2`.

| Opción | Significado | Por defecto |
|---|---|---|
| `-f frase` | frase de búsqueda | `es una prueba de` |
| `-n bits` | rango `[0, 2^bits)` (1–62) | 56 |
| `-t seg` | límite de tiempo; al cortarse imprime la estimación | 0 (sin límite) |
| `-e 1\|2` | estrategia (solo MPI) | 1 |
| `-a auto\|evp\|aesni` | implementación de AES: OpenSSL EVP o instrucciones AES-NI | auto |

Si la máquina tiene menos de 4 núcleos: `mpirun --oversubscribe -np 4 ...`.

## Compilar

```bash
sudo apt install build-essential pkg-config libssl-dev openmpi-bin libopenmpi-dev   # Debian/Ubuntu/WSL
make            # todo en bin/
```

Con Nix: `nix develop` (gcc, OpenSSL, Open MPI, gdb, valgrind) y luego `make`;
`nix run .#busqueda_mpi -- -i datos/prueba.txt -k 123456` también funciona.

| Comando | Descripción |
|---|---|
| `make` | Compila todo (`lab03` + `proyecto02`) |
| `make test` | Pruebas automáticas con rangos pequeños: ambas estrategias, ambos motores AES, P = 1..5, casos límite |
| `make demo` | Las pruebas a), b) y c) con `-np 4` y ambas estrategias (`LIMITE=30` s) |
| `make bitacora` | Todas las mediciones del Anexo 2 → `resultados/` (salidas, `tiempos.csv`, `resumen.md`, capturas PNG) |
| `make run-mpi PROG=busqueda_mpi NP=4 ARGS="..."` | `mpirun -np 4 ./bin/busqueda_mpi ...` |
| `make lab03` / `run-original` / `run-mejorado` | Programas del Laboratorio 03 |
| `make clean` | Borra `build/` y `bin/` |

`scripts/bitacora.sh` acepta `NP`, `REPS`, `REPS_LARGAS`, `LIMITE` y `BITS_ESCALA`
como variables de entorno (ver el encabezado del script).

## Estructura

```
.
├── flake.nix                  entorno reproducible (gcc, OpenSSL, Open MPI)
├── Makefile                   compilación, pruebas y bitácora
├── datos/prueba.txt           "Esta es una prueba de proyecto 2" (32 B)
├── include/
│   ├── aes_texto.h            API del Proyecto 02 (archivos, AES, búsqueda, reparto)
│   └── aes_busqueda.h         API del programa mejorado del Laboratorio 03
├── src/
│   ├── common/aes_texto.c     rutinas compartidas por los tres programas del P02
│   ├── common/aes_busqueda.c  rutinas del Laboratorio 03
│   ├── lab03/                 secuencial original y mejorado del Laboratorio 03
│   └── proyecto02/            aes_texto.c, busqueda_secuencial.c, busqueda_mpi.c
├── scripts/
│   ├── pruebas.sh             make test
│   ├── bitacora.sh            make bitacora
│   ├── resumen.py             tablas de promedios y Speedup a partir del CSV
│   └── captura_terminal.py    convierte la salida de un comando en una captura PNG
├── resultados/                salidas, tiempos y capturas de la bitácora
└── docs/
    ├── enunciados/            enunciado del Proyecto 02
    ├── lab03/                 teoría, análisis y capturas del Lab 03
    └── proyecto02/            informe (docx y PDF), estrategia alternativa, figuras
```
