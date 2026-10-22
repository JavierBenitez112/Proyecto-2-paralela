#!/usr/bin/env bash
#-----------------------------------------------------------------------
# CC3069 - Proyecto 02 - Bitacora de pruebas (Anexo 2 del informe)
#
# Ejecuta todas las pruebas del enunciado y guarda, para cada una, la
# salida exacta del programa (con el comando en la primera linea), una
# captura PNG de esa salida y una fila en resultados/tiempos.csv.
#
#   1  compilacion
#   2  cifrar y 3 descifrar "Esta es una prueba de proyecto 2" (clave 123456)
#   4  busqueda a) b) c) con -np 4, estrategias 1 y 2 (REPS repeticiones)
#   5  version secuencial con las mismas claves (referencia del Speedup)
#   6  escalabilidad: clave fija con -n BITS_ESCALA y P = 1, 2, 4 procesos
#   7  motor AES: secuencial con OpenSSL EVP vs instrucciones AES-NI
#
# Las busquedas que no terminan en un tiempo razonable (b y c con la
# estrategia 1 y con el secuencial) se cortan con -t LIMITE y el programa
# imprime la estimacion del tiempo total a partir de la tasa medida.
#
# Uso:  scripts/bitacora.sh            (desde la raiz del proyecto)
#   Variables: NP=4 REPS=5 REPS_LARGAS=1 LIMITE=60 BITS_ESCALA=24 MPIRUN="mpirun"
#              MPI_FLAGS="--oversubscribe" (si hay menos de 4 nucleos)
#-----------------------------------------------------------------------
set -euo pipefail

NP=${NP:-4}
REPS=${REPS:-5}
REPS_LARGAS=${REPS_LARGAS:-1}
LIMITE=${LIMITE:-60}
BITS_ESCALA=${BITS_ESCALA:-24}
MPIRUN=${MPIRUN:-mpirun}
MPI_FLAGS=${MPI_FLAGS:-}
# Como root (contenedores) Open MPI exige confirmacion; por variables de entorno
# para que no aparezca en los comandos de las capturas.
if [ "$(id -u)" = 0 ]; then export OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1; fi
if [ "$(getconf _NPROCESSORS_ONLN)" -lt "$NP" ]; then MPI_FLAGS="$MPI_FLAGS --oversubscribe"; fi

TEXTO=datos/prueba.txt
OUT=resultados
CAP=$OUT/capturas
mkdir -p "$OUT/salidas" "$CAP"
CSV=$OUT/tiempos.csv
echo "prueba,programa,estrategia,procesos,clave,repeticion,resultado,tiempo_s,tasa_claves_s,estimacion" > "$CSV"

CLAVE_A=123456
CLAVE_B=18014398509481984     # 2^56/4
CLAVE_C=18014398509481985     # 2^56/4 + 1

# Ejecuta un comando, guarda "$ comando" + salida en el archivo dado y la muestra.
correr() {
    local archivo=$1; shift
    { echo "\$ $*"; "$@" 2>&1; } | tee "$archivo"
}

# Agrega una fila al CSV a partir de la salida guardada.
registrar() {
    local archivo=$1 prueba=$2 programa=$3 estr=$4 np=$5 clave=$6 rep=$7
    local res tiempo tasa est
    res=$(grep -m1 '^Resultado:' "$archivo" | sed 's/^Resultado: *//; s/,//g')
    tiempo=$(grep -m1 '^Tiempo de busqueda:' "$archivo" | awk '{print $4}')
    tasa=$(grep -m1 -E '^Tasa' "$archivo" | grep -oE '[0-9]+ claves/s' | head -1 | awk '{print $1}')
    est=$(grep -A1 -m1 '^Estimacion' "$archivo" | grep -oE -- '-> [0-9.]+ s( \(~[^)]*\))?' \
          | tail -1 | sed 's/^-> //' | tr ',' ' ' || true)
    echo "$prueba,$programa,$estr,$np,$clave,$rep,$res,$tiempo,$tasa,$est" >> "$CSV"
}

captura() {   # captura <txt> <titulo>
    python3 scripts/captura_terminal.py "$1" "$CAP/$(basename "${1%.txt}").png" "$2"
}

echo "== 1. Compilacion"
make clean > /dev/null
correr "$OUT/salidas/01_compilacion.txt" make
captura "$OUT/salidas/01_compilacion.txt" "Compilacion (make)"

echo "== 2-3. Cifrado y descifrado"
correr "$OUT/salidas/02_cifrar.txt" ./bin/aes_texto -e $CLAVE_A -i $TEXTO -o $OUT/cifrado.bin
captura "$OUT/salidas/02_cifrar.txt" "Inciso 1: cifrar"
correr "$OUT/salidas/03_descifrar.txt" ./bin/aes_texto -d $CLAVE_A -i $OUT/cifrado.bin -o $OUT/recuperado.txt
captura "$OUT/salidas/03_descifrar.txt" "Inciso 1: descifrar"
correr "$OUT/salidas/03b_clave_incorrecta.txt" ./bin/aes_texto -d 123457 -i $OUT/cifrado.bin -o $OUT/basura.txt || true
captura "$OUT/salidas/03b_clave_incorrecta.txt" "Inciso 1: clave incorrecta"

# Las claves b) y c) con estrategia 1 y con el secuencial requieren ~2^52 y
# ~2^54 intentos: se cortan con el limite de tiempo.
n=4
for letra in a b c; do
    case $letra in a) clave=$CLAVE_A ;; b) clave=$CLAVE_B ;; c) clave=$CLAVE_C ;; esac
    for e in 1 2; do
        reps=$REPS; lim=()
        if [ $letra != a ] && [ $e = 1 ]; then reps=$REPS_LARGAS; lim=(-t "$LIMITE"); fi
        echo "== 4. Busqueda $letra) clave=$clave estrategia $e (-np $NP, $reps rep.)"
        for r in $(seq 1 "$reps"); do
            f=$(printf "%s/salidas/%02d_busqueda_%s_e%s_r%d.txt" $OUT $n $letra $e "$r")
            # shellcheck disable=SC2086
            correr "$f" $MPIRUN $MPI_FLAGS -np "$NP" ./bin/busqueda_mpi -i $TEXTO -k $clave -e $e "${lim[@]}"
            registrar "$f" "$letra" mpi "$e" "$NP" $clave "$r"
            [ "$r" = 1 ] && captura "$f" "Inciso 2$letra) estrategia $e, -np $NP"
        done
        n=$((n + 1))
    done
done

for letra in a b c; do
    case $letra in a) clave=$CLAVE_A ;; b) clave=$CLAVE_B ;; c) clave=$CLAVE_C ;; esac
    reps=$REPS; lim=()
    if [ $letra != a ]; then reps=$REPS_LARGAS; lim=(-t "$LIMITE"); fi
    echo "== 5. Secuencial $letra) clave=$clave ($reps rep.)"
    for r in $(seq 1 "$reps"); do
        f=$(printf "%s/salidas/%02d_secuencial_%s_r%d.txt" $OUT $n $letra "$r")
        correr "$f" ./bin/busqueda_secuencial -i $TEXTO -k $clave "${lim[@]}"
        registrar "$f" "$letra" secuencial - 1 $clave "$r"
        [ "$r" = 1 ] && captura "$f" "Secuencial, clave $letra)"
    done
    n=$((n + 1))
done

# Escalabilidad: misma cantidad de trabajo para P = 1, 2, 4 con ambas
# estrategias. Rango 2^BITS_ESCALA y clave 2^BITS_ESCALA - 1: es la ultima candidata del
# ultimo proceso en las dos estrategias, asi que se recorre todo el rango.
CLAVE_E=$(( (1 << BITS_ESCALA) - 1 ))
echo "== 6. Escalabilidad (-n $BITS_ESCALA, clave $CLAVE_E)"
for r in $(seq 1 "$REPS"); do
    f=$(printf "%s/salidas/%02d_escala_secuencial_r%d.txt" $OUT $n "$r")
    correr "$f" ./bin/busqueda_secuencial -i $TEXTO -k $CLAVE_E -n $BITS_ESCALA
    registrar "$f" escala secuencial - 1 $CLAVE_E "$r"
done
n=$((n + 1))
for p in 1 2 4; do
    for e in 1 2; do
        for r in $(seq 1 "$REPS"); do
            f=$(printf "%s/salidas/%02d_escala_p%d_e%s_r%d.txt" $OUT $n "$p" $e "$r")
            # shellcheck disable=SC2086
            correr "$f" $MPIRUN $MPI_FLAGS -np "$p" ./bin/busqueda_mpi -i $TEXTO -k $CLAVE_E -n $BITS_ESCALA -e $e
            registrar "$f" escala mpi "$e" "$p" $CLAVE_E "$r"
        done
        n=$((n + 1))
    done
done

echo "== 7. Motor AES (EVP vs AES-NI, secuencial, -n $BITS_ESCALA)"
for m in evp aesni; do
    for r in $(seq 1 "$REPS"); do
        f=$(printf "%s/salidas/%02d_motor_%s_r%d.txt" $OUT $n $m "$r")
        correr "$f" ./bin/busqueda_secuencial -i $TEXTO -k $CLAVE_E -n $BITS_ESCALA -a $m
        registrar "$f" "motor-$m" secuencial - 1 $CLAVE_E "$r"
        [ "$r" = 1 ] && captura "$f" "Motor AES: $m"
    done
    n=$((n + 1))
done

echo "== Equipo"
{
    echo "\$ uname -srm; nproc; lscpu | grep 'Model name'"
    uname -srm; nproc; lscpu | grep 'Model name' || true
    echo "\$ $MPIRUN --version | head -1; openssl version"
    $MPIRUN --version | head -1; openssl version
} | tee "$OUT/salidas/00_equipo.txt"
captura "$OUT/salidas/00_equipo.txt" "Equipo de pruebas"

python3 scripts/resumen.py "$CSV" | tee "$OUT/resumen.md"
echo "Listo: $CSV, $OUT/resumen.md y capturas en $CAP/"
