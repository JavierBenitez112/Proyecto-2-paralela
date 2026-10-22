#!/usr/bin/env bash
#-----------------------------------------------------------------------
# CC3069 - Proyecto 02 - Compilar y probar en Windows (Git Bash + Docker)
#
# Windows no tiene Open MPI nativo, asi que todo corre en un contenedor
# Ubuntu 24.04. La primera vez construye la imagen "proyecto02-mpi" con
# gcc, OpenSSL y Open MPI (unos minutos); despues arranca en segundos.
# Si Docker Desktop esta apagado, lo inicia y espera a que responda.
#
# Uso:  ./scripts/correr_windows.sh [modo]    (en Git Bash, desde cualquier carpeta)
#   todo      compilar + make test + make demo   (por defecto)
#   compilar  solo make
#   test      make test
#   demo      make demo (LIMITE=30 s; cambiar con LIMITE=60 ./scripts/correr_windows.sh demo)
#   bitacora  make bitacora; copia resultados/ de vuelta al proyecto
#   shell     abre una terminal dentro del contenedor, ya compilado
#
# El proyecto se copia dentro del contenedor (sin build/ ni bin/ de
# Windows), asi que la compilacion nunca toca tus archivos; solo el modo
# bitacora escribe en resultados/.
#-----------------------------------------------------------------------
set -euo pipefail

MODO=${1:-todo}
IMAGEN=proyecto02-mpi
LIMITE=${LIMITE:-30}
cd "$(dirname "$0")/.."
RAIZ=$(pwd -W 2> /dev/null || pwd)   # ruta C:/... que entiende Docker

case $MODO in
    todo|compilar|test|demo|bitacora|shell) ;;
    *) echo "Modo desconocido: $MODO (todo|compilar|test|demo|bitacora|shell)" >&2; exit 1 ;;
esac

# --- Docker Desktop encendido
if ! docker info > /dev/null 2>&1; then
    echo "== Iniciando Docker Desktop..."
    for exe in "$LOCALAPPDATA/Programs/DockerDesktop/Docker Desktop.exe" \
               "/c/Program Files/Docker/Docker/Docker Desktop.exe"; do
        [ -f "$exe" ] && { "$exe" > /dev/null 2>&1 & break; }
    done
    for _ in $(seq 60); do
        docker info > /dev/null 2>&1 && break
        sleep 3
    done
    docker info > /dev/null 2>&1 || { echo "Docker no respondio. Abre Docker Desktop y reintenta." >&2; exit 1; }
fi

# --- Imagen con las dependencias (solo la primera vez)
if ! docker image inspect "$IMAGEN" > /dev/null 2>&1; then
    echo "== Construyendo la imagen $IMAGEN (solo la primera vez)..."
    docker build -t "$IMAGEN" - << 'EOF'
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential pkg-config libssl-dev openmpi-bin libopenmpi-dev \
        python3 python3-pil fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
ENV OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
WORKDIR /p
EOF
fi

# --- Comando dentro del contenedor
PREPARAR='cp -r /src/. /p && rm -rf /p/build /p/bin \
  && find /p/scripts -type f \( -name "*.sh" -o -name "*.py" \) -exec sed -i "s/\r$//" {} + \
  && echo "== Compilando" && make'
case $MODO in
    compilar) CMD="$PREPARAR" ;;
    test)     CMD="$PREPARAR && make test" ;;
    demo)     CMD="$PREPARAR && make demo LIMITE=$LIMITE" ;;
    todo)     CMD="$PREPARAR && make test && make demo LIMITE=$LIMITE" ;;
    bitacora) CMD="$PREPARAR && make bitacora && rm -rf /src/resultados && cp -r /p/resultados /src/" ;;
    shell)    CMD="$PREPARAR && echo && echo 'Listo. Prueba: mpirun --oversubscribe -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k 123456' && exec bash" ;;
esac

# MSYS_NO_PATHCONV evita que Git Bash reescriba /src y /p como rutas de Windows
TTY=-i; [ -t 0 ] && [ -t 1 ] && TTY=-it
MSYS_NO_PATHCONV=1 docker run --rm $TTY -v "$RAIZ:/src" "$IMAGEN" bash -c "$CMD"
