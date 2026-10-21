/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Proyecto 02 (inciso 2)
 * busqueda_secuencial.c - Fuerza bruta AES-128 con un solo proceso
 *
 * Version de referencia para el Speedup: recorre k = 0, 1, 2, ... < 2^bits,
 * descifra el mensaje con la clave de 128 bits de cada candidata y se
 * detiene cuando el texto descifrado contiene la frase de busqueda.
 *
 * Uso:
 *   busqueda_secuencial -i texto.txt -k CLAVE [-f frase] [-n bits] [-t seg]
 *   busqueda_secuencial -c cifrado.bin        [-f frase] [-n bits] [-t seg]
 *   (-a auto|evp|aesni elige la implementacion de AES; por defecto auto)
 *
 *   Con -i/-k el programa cifra el texto con CLAVE solo para preparar la
 *   prueba; la busqueda no la usa (unicamente se reporta para estimar el
 *   tiempo cuando -t corta una busqueda demasiado larga).
 *----------------------------------------------------------------------*/
#include "aes_texto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void uso(const char *prog)
{
    fprintf(stderr,
        "Uso: %s (-i texto.txt -k CLAVE | -c cifrado.bin) [-f frase] [-n bits] [-t seg]\n"
        "        [-a auto|evp|aesni]\n"
        "     frase por defecto \"%s\", bits por defecto %d, -t 0 = sin limite\n",
        prog, FRASE_DEFECTO, BITS_DEFECTO);
}

int main(int argc, char **argv)
{
    opciones_t op;
    char error[160];
    if (parse_opciones(argc, argv, 0, &op, error, sizeof error) != 0) {
        fprintf(stderr, "Error: %s\n", error);
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    mensaje_t msg;
    if (preparar_mensaje(&op, &msg) != 0)
        return EXIT_FAILURE;

    const unsigned char *frase = (const unsigned char *)op.frase;
    size_t flen = strlen(op.frase);
    buscador_t bus;
    if (buscador_iniciar(&bus, msg.iv, msg.cifrado.datos, (int)msg.cifrado.len,
                         frase, flen, op.motor) != 0) {
        mensaje_liberar(&msg);
        return EXIT_FAILURE;
    }

    const uint64_t total = UINT64_C(1) << op.bits;
    printf("=== Busqueda AES-128 por fuerza bruta (secuencial) ===\n");
    printf("Rango:                [0, 2^%u) = %" PRIu64 " candidatas\n", op.bits, total);
    printf("Frase de busqueda:    \"%s\"\n", op.frase);
    printf("Motor AES:            %s\n", nombre_motor(bus.motor));
    printf("Texto cifrado:        ");
    imprimir_hex(msg.cifrado.datos, msg.cifrado.len < 64 ? msg.cifrado.len : 64);
    printf("  [%zu B]\n", msg.cifrado.len);
    if (op.limite > 0)
        printf("Limite de tiempo:     %.1f s\n", op.limite);

    /* ------------------------------------------------ busqueda */
    int encontrada = 0, error_aes = 0, por_limite = 0;
    uint64_t k = 0, probadas = 0;
    double inicio = reloj();

    while (k < total && !encontrada && !error_aes) {
        uint64_t fin_lote = total - k > INTERVALO_REVISION ? k + INTERVALO_REVISION : total;
        for (; k < fin_lote; k++) {
            int r = probar_clave(&bus, k);
            if (r != 0) {
                if (r > 0) encontrada = 1; else error_aes = 1;
                break;
            }
        }
        if (!encontrada && op.limite > 0 && reloj() - inicio >= op.limite) {
            por_limite = 1;
            break;
        }
    }
    double tiempo = reloj() - inicio;
    probadas = k + (uint64_t)encontrada;   /* k es la candidata en curso */

    /* ------------------------------------------------ resultado */
    int estado = EXIT_SUCCESS;
    double tasa = tiempo > 0 ? (double)probadas / tiempo : 0.0;
    if (error_aes) {
        reportar_error("OpenSSL fallo durante la busqueda");
        estado = EXIT_FAILURE;
    } else if (encontrada) {
        unsigned char clave[AES_KEY_LEN];
        clave_desde_u64(k, clave);
        unsigned char *texto = malloc(msg.cifrado.len);
        int plen = texto ? descifrar_texto(clave, msg.iv, msg.cifrado.datos,
                                           (int)msg.cifrado.len, texto) : -1;
        printf("Resultado:            CLAVE ENCONTRADA\n");
        printf("Clave (decimal):      %" PRIu64 "\n", k);
        printf("Clave AES-128:        ");
        imprimir_hex(clave, AES_KEY_LEN);
        printf("\n");
        if (plen >= 0) {
            printf("Texto descifrado:     \"");
            imprimir_texto(texto, (size_t)plen);
            printf("\"\n");
        } else {
            printf("Texto descifrado:     (relleno invalido: posible falso positivo)\n");
        }
        free(texto);
    } else if (por_limite) {
        printf("Resultado:            NO ENCONTRADA (se alcanzo el limite de %.1f s)\n", op.limite);
    } else {
        printf("Resultado:            NO ENCONTRADA en todo el rango\n");
    }
    printf("Claves probadas:      %" PRIu64 "\n", probadas);
    printf("Tiempo de busqueda:   ");
    imprimir_duracion(tiempo);
    printf("\nTasa:                 %.0f claves/s\n", tasa);

    if (por_limite && msg.tiene_referencia && msg.clave_referencia < total && tasa > 0) {
        /* Estimacion: el secuencial necesita clave+1 intentos. */
        printf("Estimacion para la clave %" PRIu64 ": %" PRIu64 " intentos -> ",
               msg.clave_referencia, msg.clave_referencia + 1);
        imprimir_duracion((double)(msg.clave_referencia + 1) / tasa);
        printf("\n");
    }

    buscador_liberar(&bus);
    mensaje_liberar(&msg);
    return estado;
}
