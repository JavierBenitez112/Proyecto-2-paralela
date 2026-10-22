#!/usr/bin/env bash
#-----------------------------------------------------------------------
# CC3069 - Proyecto 02 - Compilar y probar en macOS (Intel o Apple Silicon)
#
# Instala con Homebrew lo que falte (Open MPI, OpenSSL 3, pkg-config),
# compila con make y ejecuta lo pedido.
#
# Uso:  ./scripts/correr_mac.sh [modo]        (desde cualquier carpeta)
#   todo      compilar + make test + make demo   (por defecto)
#   compilar  solo make
#   test      make test
#   demo      make demo (LIMITE=30 s; cambiar con LIMITE=60 ./scripts/correr_mac.sh demo)
#   bitacora  make bitacora -> resultados/ (instala Pillow para las capturas)
#
# En Apple Silicon no hay AES-NI: el motor "auto" usa OpenSSL EVP.
#-----------------------------------------------------------------------
set -euo pipefail

MODO=${1:-todo}
cd "$(dirname "$0")/.."

if ! command -v brew > /dev/null; then
    echo "Falta Homebrew. Instalalo con:" >&2
    echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' >&2
    exit 1
fi

for paquete in open-mpi openssl@3 pkg-config; do
    brew list --versions "$paquete" > /dev/null || brew install "$paquete"
done
# OpenSSL de Homebrew es keg-only: se lo indicamos a pkg-config
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LIMITE=${LIMITE:-30}

echo "== Compilando"
make

case $MODO in
    compilar) ;;
    test)     make test ;;
    demo)     make demo LIMITE="$LIMITE" ;;
    todo)     make test && make demo LIMITE="$LIMITE" ;;
    bitacora)
        python3 -c 'import PIL' 2> /dev/null || python3 -m pip install --user Pillow
        make bitacora ;;
    *) echo "Modo desconocido: $MODO (todo|compilar|test|demo|bitacora)" >&2; exit 1 ;;
esac
