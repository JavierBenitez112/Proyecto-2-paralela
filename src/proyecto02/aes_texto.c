/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Proyecto 02 (inciso 1)
 * aes_texto.c - Cifra y descifra un archivo de texto con AES-128-CBC
 *
 * Uso:
 *   aes_texto -e CLAVE -i texto.txt   -o cifrado.bin  [-v IV_HEX]   cifrar
 *   aes_texto -d CLAVE -i cifrado.bin -o recuperado.txt             descifrar
 *   aes_texto -r CLAVE -i texto.txt  [-o cifrado.bin]               ida y vuelta
 *
 *   CLAVE   numero decimal (o 0x...) que se convierte a 128 bits en big
 *           endian con ceros a la izquierda (123456 -> 0...01e240), o
 *           32 digitos hexadecimales con el prefijo "hex:" (clave completa).
 *   -v      IV en hexadecimal (32 digitos); por defecto se genera con
 *           RAND_bytes. El IV se guarda al inicio del archivo cifrado.
 *
 * Formato de cifrado.bin: IV (16 B) || AES-128-CBC(texto, PKCS#7).
 * Codigo de salida: 0 si todo bien, 1 si hay error de uso, de archivo o
 * si la clave no descifra el archivo (relleno invalido).
 *----------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L

#include "aes_texto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <openssl/rand.h>

#define MAX_HEX_MOSTRAR 96   /* bytes de cifrado que se muestran en pantalla */

enum modo { MODO_NINGUNO, MODO_CIFRAR, MODO_DESCIFRAR, MODO_IDA_VUELTA };

static void uso(const char *prog)
{
    fprintf(stderr,
        "Uso:\n"
        "  %s -e CLAVE -i texto.txt   -o cifrado.bin [-v IV_HEX]   cifrar\n"
        "  %s -d CLAVE -i cifrado.bin -o recuperado.txt            descifrar\n"
        "  %s -r CLAVE -i texto.txt  [-o cifrado.bin]              ida y vuelta\n"
        "CLAVE: entero (123456, 0x1e240) o hex:<32 digitos hex>\n",
        prog, prog, prog);
}

/* Interpreta CLAVE: entero -> clave_desde_u64, o "hex:" + 32 digitos. */
static int leer_clave(const char *s, unsigned char clave[AES_KEY_LEN])
{
    if (strncmp(s, "hex:", 4) == 0)
        return parse_clave_hex(s + 4, clave);
    uint64_t k;
    if (parse_u64_seguro(s, &k) != 0)
        return -1;
    clave_desde_u64(k, clave);
    return 0;
}

/* Muestra hasta MAX_HEX_MOSTRAR bytes en hexadecimal. */
static void mostrar_hex(const char *etiqueta, const unsigned char *d, size_t n)
{
    printf("%-22s", etiqueta);
    imprimir_hex(d, n < MAX_HEX_MOSTRAR ? n : MAX_HEX_MOSTRAR);
    if (n > MAX_HEX_MOSTRAR)
        printf("... (+%zu B)", n - MAX_HEX_MOSTRAR);
    printf("  [%zu B]\n", n);
}

static void mostrar_texto(const char *etiqueta, const unsigned char *d, size_t n)
{
    printf("%-22s\"", etiqueta);
    imprimir_texto(d, n);
    printf("\"  [%zu B]\n", n);
}

/* Cifra el archivo de entrada. Si salida != NULL guarda IV || cifrado.
 * Deja el cifrado en *cifrado (el llamador lo libera). 0 / -1. */
static int cifrar_archivo(const unsigned char clave[AES_KEY_LEN],
                          const unsigned char iv[AES_IV_LEN],
                          const buffer_t *texto, const char *salida,
                          buffer_t *archivo)
{
    archivo->len = AES_IV_LEN + texto->len + AES_BLOCK_LEN;
    archivo->datos = malloc(archivo->len);
    if (archivo->datos == NULL) {
        reportar_error("memoria insuficiente");
        return -1;
    }
    memcpy(archivo->datos, iv, AES_IV_LEN);
    int clen = cifrar_texto(clave, iv, texto->datos, (int)texto->len,
                            archivo->datos + AES_IV_LEN);
    if (clen < 0) {
        buffer_liberar(archivo);
        return -1;
    }
    archivo->len = AES_IV_LEN + (size_t)clen;

    mostrar_texto("Texto original:", texto->datos, texto->len);
    mostrar_hex("Texto cifrado:", archivo->datos + AES_IV_LEN, (size_t)clen);
    if (salida != NULL) {
        if (escribir_archivo(salida, archivo->datos, archivo->len) != 0) {
            buffer_liberar(archivo);
            return -1;
        }
        printf("%-22s%s (IV + cifrado, %zu B)\n", "Archivo cifrado:", salida, archivo->len);
    }
    return 0;
}

/* Descifra un buffer con formato IV || cifrado; si salida != NULL guarda
 * el texto recuperado. Deja el texto en *texto. 0 / -1. */
static int descifrar_archivo(const unsigned char clave[AES_KEY_LEN],
                             const buffer_t *archivo, const char *salida,
                             buffer_t *texto)
{
    if (archivo->len < AES_IV_LEN + AES_BLOCK_LEN ||
        (archivo->len - AES_IV_LEN) % AES_BLOCK_LEN != 0) {
        reportar_error("el archivo no tiene el formato IV (16 B) + cifrado (n*16 B)");
        return -1;
    }
    const unsigned char *iv = archivo->datos;
    const unsigned char *cif = archivo->datos + AES_IV_LEN;
    int clen = (int)(archivo->len - AES_IV_LEN);

    texto->datos = malloc((size_t)clen);
    if (texto->datos == NULL) {
        reportar_error("memoria insuficiente");
        return -1;
    }
    mostrar_hex("Texto cifrado:", cif, (size_t)clen);
    int plen = descifrar_texto(clave, iv, cif, clen, texto->datos);
    if (plen < 0) {
        fprintf(stderr, "Error: la clave no descifra el archivo (relleno PKCS#7 "
                        "invalido): clave incorrecta o archivo danado\n");
        buffer_liberar(texto);
        return -1;
    }
    texto->len = (size_t)plen;
    mostrar_texto("Texto recuperado:", texto->datos, texto->len);
    if (salida != NULL) {
        if (escribir_archivo(salida, texto->datos, texto->len) != 0) {
            buffer_liberar(texto);
            return -1;
        }
        printf("%-22s%s\n", "Archivo recuperado:", salida);
    }
    return 0;
}

int main(int argc, char **argv)
{
    enum modo modo = MODO_NINGUNO;
    const char *arg_clave = NULL, *entrada = NULL, *salida = NULL, *arg_iv = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "e:d:r:i:o:v:h")) != -1) {
        switch (opt) {
        case 'e': case 'd': case 'r':
            if (modo != MODO_NINGUNO) {
                fprintf(stderr, "Error: use solo uno de -e, -d o -r\n");
                return EXIT_FAILURE;
            }
            modo = opt == 'e' ? MODO_CIFRAR : opt == 'd' ? MODO_DESCIFRAR : MODO_IDA_VUELTA;
            arg_clave = optarg;
            break;
        case 'i': entrada = optarg; break;
        case 'o': salida = optarg; break;
        case 'v': arg_iv = optarg; break;
        case 'h': uso(argv[0]); return EXIT_SUCCESS;
        default:  uso(argv[0]); return EXIT_FAILURE;
        }
    }
    if (modo == MODO_NINGUNO || entrada == NULL || optind != argc ||
        (modo != MODO_IDA_VUELTA && salida == NULL)) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    unsigned char clave[AES_KEY_LEN], iv[AES_IV_LEN];
    if (leer_clave(arg_clave, clave) != 0) {
        fprintf(stderr, "Error: clave invalida '%s' (entero de 64 bits o hex:<32 hex>)\n",
                arg_clave);
        return EXIT_FAILURE;
    }
    if (arg_iv != NULL) {
        if (modo == MODO_DESCIFRAR) {
            fprintf(stderr, "Error: -v solo aplica al cifrar (el IV va en el archivo)\n");
            return EXIT_FAILURE;
        }
        if (parse_clave_hex(arg_iv, iv) != 0) {
            fprintf(stderr, "Error: IV invalido (se esperan 32 digitos hex)\n");
            return EXIT_FAILURE;
        }
    } else if (RAND_bytes(iv, AES_IV_LEN) != 1) {
        reportar_error("RAND_bytes no pudo generar el IV");
        return EXIT_FAILURE;
    }

    buffer_t in = {0}, cifrado = {0}, recuperado = {0};
    int estado = EXIT_FAILURE;
    /* Un .bin mide texto + IV + hasta un bloque de relleno. */
    size_t max = modo == MODO_DESCIFRAR ? MAX_TEXTO + AES_IV_LEN + AES_BLOCK_LEN : MAX_TEXTO;
    if (leer_archivo(entrada, max, &in) != 0)
        goto fin;

    printf("=== AES-128-CBC (%s) ===\n",
           modo == MODO_CIFRAR ? "cifrar" : modo == MODO_DESCIFRAR ? "descifrar" : "ida y vuelta");
    printf("%-22s%s\n", "Clave (parametro):", arg_clave);
    mostrar_hex("Clave AES-128:", clave, AES_KEY_LEN);

    if (modo == MODO_DESCIFRAR) {
        mostrar_hex("IV (del archivo):", in.datos, in.len < AES_IV_LEN ? in.len : AES_IV_LEN);
        if (descifrar_archivo(clave, &in, salida, &recuperado) != 0)
            goto fin;
    } else {
        mostrar_hex("IV:", iv, AES_IV_LEN);
        if (cifrar_archivo(clave, iv, &in, salida, &cifrado) != 0)
            goto fin;
        if (modo == MODO_IDA_VUELTA) {
            if (descifrar_archivo(clave, &cifrado, NULL, &recuperado) != 0)
                goto fin;
            int igual = recuperado.len == in.len &&
                        memcmp(recuperado.datos, in.datos, in.len) == 0;
            printf("%-22s%s\n", "Verificacion:",
                   igual ? "OK, el texto recuperado es identico al original"
                         : "FALLO, el texto recuperado no coincide");
            if (!igual)
                goto fin;
        }
    }
    estado = EXIT_SUCCESS;

fin:
    buffer_liberar(&in);
    buffer_liberar(&cifrado);
    buffer_liberar(&recuperado);
    return estado;
}
