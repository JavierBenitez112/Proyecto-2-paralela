#!/usr/bin/env bash
#-----------------------------------------------------------------------
# CC3069 - Proyecto 02 - Pruebas automaticas (make test)
#
# Verifica con rangos pequenos (rapido) que:
#   - aes_texto cifra y descifra (ida y vuelta) y rechaza una clave mala;
#   - secuencial y MPI encuentran la misma clave con ambos motores AES y
#     ambas estrategias, para P = 1..5 y claves en posiciones dificiles
#     (0, ultima del rango, limites de bloque, n no divisible entre P);
#   - una clave fuera del rango da "NO ENCONTRADA";
#   - se validan los argumentos incorrectos.
#-----------------------------------------------------------------------
set -uo pipefail

MPIRUN=${MPIRUN:-mpirun}
FLAGS="--oversubscribe"
[ "$(id -u)" = 0 ] && export OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
TXT=datos/prueba.txt
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fallos=0 total=0

ok()   { total=$((total + 1)); echo "  ok   $1"; }
mal()  { total=$((total + 1)); fallos=$((fallos + 1)); echo "  FALLO $1"; }
espera_clave() {   # espera_clave <descripcion> <clave> <comando...>
    local desc=$1 clave=$2; shift 2
    local salida
    salida=$("$@" 2>&1)
    if grep -q "^Clave (decimal): *$clave\$" <<< "$salida" &&
       grep -q 'Texto descifrado: *"Esta es una prueba de proyecto 2"' <<< "$salida"; then
        ok "$desc"
    else
        mal "$desc"; echo "$salida" | sed 's/^/        /'
    fi
}

echo "== aes_texto"
if ./bin/aes_texto -r 123456 -i $TXT | grep -q 'Verificacion: *OK'; then ok "ida y vuelta"; else mal "ida y vuelta"; fi
./bin/aes_texto -e 0x1e240 -i $TXT -o "$TMP/c.bin" > /dev/null
./bin/aes_texto -d 123456 -i "$TMP/c.bin" -o "$TMP/r.txt" > /dev/null
if cmp -s $TXT "$TMP/r.txt"; then ok "cifrar (hex) / descifrar (decimal) a archivo"; else mal "cifrar/descifrar a archivo"; fi
if ./bin/aes_texto -d 123457 -i "$TMP/c.bin" -o "$TMP/x.txt" > /dev/null 2>&1; then
    mal "clave incorrecta rechazada"; else ok "clave incorrecta rechazada"; fi
if ./bin/aes_texto -e 1 -i /no/existe -o "$TMP/y" > /dev/null 2>&1; then
    mal "archivo inexistente"; else ok "archivo inexistente"; fi

echo "== busqueda (rango 2^16)"
for motor in evp aesni; do
    for k in 0 1 12345 32767 32768 49151 65535; do
        espera_clave "secuencial -a $motor k=$k" $k ./bin/busqueda_secuencial -i $TXT -k $k -n 16 -a $motor
        for p in 1 2 3 4 5; do
            for e in 1 2; do
                espera_clave "mpi -np $p -e $e -a $motor k=$k" $k \
                    $MPIRUN $FLAGS -np $p ./bin/busqueda_mpi -i $TXT -k $k -n 16 -e $e -a $motor
            done
        done
    done
done

echo "== archivo cifrado como entrada (-c)"
./bin/aes_texto -e 4242 -i $TXT -o "$TMP/k.bin" > /dev/null
espera_clave "secuencial -c" 4242 ./bin/busqueda_secuencial -c "$TMP/k.bin" -n 16
espera_clave "mpi -c -e 2" 4242 $MPIRUN $FLAGS -np 4 ./bin/busqueda_mpi -c "$TMP/k.bin" -n 16 -e 2

echo "== casos limite"
if $MPIRUN $FLAGS -np 4 ./bin/busqueda_mpi -i $TXT -k 70000 -n 16 -e 2 | grep -q 'NO ENCONTRADA en todo el rango'; then
    ok "clave fuera del rango"; else mal "clave fuera del rango"; fi
if $MPIRUN $FLAGS -np 4 ./bin/busqueda_mpi -i $TXT -k 18014398509481984 -e 1 -t 1 | grep -q 'Estimacion'; then
    ok "limite de tiempo con estimacion"; else mal "limite de tiempo con estimacion"; fi
for args in "-i $TXT" "-c a -k 1" "-i $TXT -k 1 -n 70" "-i $TXT -k 1 -e 3" "-i $TXT -k abc" "-i $TXT -k 1 -a xyz" "-i $TXT -k 1 -t -5"; do
    # shellcheck disable=SC2086
    if $MPIRUN $FLAGS -np 2 ./bin/busqueda_mpi $args > /dev/null 2>&1; then
        mal "rechaza: $args"; else ok "rechaza: $args"; fi
done

echo
echo "$((total - fallos))/$total pruebas correctas"
[ "$fallos" = 0 ]
