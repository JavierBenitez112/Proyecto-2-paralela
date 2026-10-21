# CC3069 – Proyecto 02: búsqueda de claves AES-128 con Open MPI

Universidad del Valle de Guatemala · Computación Paralela y Distribuida · Ciclo 2, 2026

## Estructura

```
.
├── flake.nix / flake.lock      entorno reproducible (gcc, OpenSSL, Open MPI)
├── Makefile                    compilación de todos los programas → bin/
├── include/
│   └── aes_busqueda.h          API compartida (clave, cifrado, prueba de candidatas)
├── src/
│   ├── common/                 rutinas compartidas por secuencial y MPI
│   ├── lab03/                  programas del Laboratorio 03 (original y mejorado)
│   └── proyecto02/             programas MPI del Proyecto 02 (cada .c → bin/<nombre>)
└── docs/
    ├── enunciados/             enunciados del Lab 03 y del Proyecto 02
    ├── lab03/                  teoría, análisis, reparto y capturas del Lab 03
    └── proyecto02/             informe, estrategia alternativa y diagramas
```

## Uso con Nix

```bash
nix develop            # shell con gcc, make, OpenSSL, Open MPI, gdb, valgrind
make                   # compila todo en bin/

nix build              # paquete con los binarios en ./result/bin
nix run                # secuencial mejorado
nix run .#original     # secuencial original del enunciado
```

## Uso sin Nix (Debian/Ubuntu/WSL)

```bash
sudo apt install build-essential pkg-config libssl-dev openmpi-bin libopenmpi-dev
make
```

## Objetivos del Makefile

| Comando | Descripción |
|---|---|
| `make` | Compila todo (`lab03` + `proyecto02`) |
| `make lab03` | Secuencial original y mejorado |
| `make proyecto02` | Todos los `src/proyecto02/*.c` con `mpicc` |
| `make run-original` | Ejecuta el secuencial original |
| `make run-mejorado ARGS="-k 12345"` | Ejecuta el secuencial mejorado |
| `make run-mpi PROG=<nombre> NP=4 ARGS="..."` | `mpirun -np 4 ./bin/<nombre> ...` |
| `make clean` | Borra `build/` y `bin/` |

Para agregar un programa MPI basta con crear `src/proyecto02/<nombre>.c`
(con `#include "aes_busqueda.h"` para reutilizar las rutinas comunes);
el Makefile lo detecta y genera `bin/<nombre>`.
