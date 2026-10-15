/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Laboratorio 03
 * Busqueda secuencial de clave AES-128 (version mejorada, Parte 2)
 *
 * Uso: ./busqueda_clave_aes_secuencial_mejorado [-n bits] [-k secreta] [-m mensaje]
 *   -n bits     el rango explorado es [0, 2^bits)            (def. 20)
 *   -k secreta  clave secreta (candidata) a cifrar, < 2^bits (def. aleatoria)
 *   -m mensaje  texto a cifrar, hasta 1024 bytes             (def. "Puedes lograrlo!")
 *
 * Mejoras (autor entre corchetes):
 *   M1 [Javier Benitez] Parametros por linea de comandos (rango, secreta, mensaje).
 *   M2 [Javier Benitez] Secreta aleatoria con RAND_bytes dentro del rango.
 *   M3 [Javier Benitez] AES-128-CBC con IV aleatorio y relleno PKCS#7 (mensajes
 *                       de cualquier largo; ya no es ECB).
 *   M4 [Javier Benitez] Clave de 128 bits real: 8 bytes superiores aleatorios y
 *                       conocidos (prefijo) + 8 bytes inferiores = candidata.
 *   M5 [Javier Benitez] Rendimiento: se descifra solo el 1er bloque y se compara
 *                       con el texto conocido; el descifrado completo (con
 *                       validacion del relleno) solo ocurre si coincide.
 *   M6 [Javier Benitez] Validacion de argumentos y reloj monotono.
 *   M7 [Javier Lopez]  Rendimiento: EVP_DecryptInit_ex con cipher=NULL; AES-CBC se
 *                       configura una sola vez y por candidata solo se cambia la clave.
 *   (La mejora de P3 se integrara aqui; ver REPARTO.md.)
 *----------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#define MAX_MSG 1024
#define BLOCK 16

static void fail(const char *d)
{
    fprintf(stderr, "%s\n", d);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

/* M4: clave = prefijo (8 B, conocido) || candidata (8 B, big endian). */
static void make_key(const unsigned char prefix[8], uint64_t cand,
                     unsigned char key[16])
{
    memcpy(key, prefix, 8);
    for (int i = 0; i < 8; i++)
        key[15 - i] = (unsigned char)(cand >> (8 * i));
}

static double get_time(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("reloj");
        exit(EXIT_FAILURE);
    }
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Cifra con AES-128-CBC + PKCS#7. Devuelve la longitud del cifrado. */
static int encrypt(const unsigned char key[16], const unsigned char iv[16],
                   const unsigned char *in, int len, unsigned char *out)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0, f = 0;
    if (!c || EVP_EncryptInit_ex(c, EVP_aes_128_cbc(), NULL, key, iv) != 1 ||
        EVP_EncryptUpdate(c, out, &n, in, len) != 1 ||
        EVP_EncryptFinal_ex(c, out + n, &f) != 1)
        fail("Error al cifrar.");
    EVP_CIPHER_CTX_free(c);
    return n + f;
}

/* Descifra y valida el relleno. Devuelve la longitud del texto, o -1 si la
 * clave es incorrecta (relleno invalido). */
static int decrypt_full(EVP_CIPHER_CTX *c, const unsigned char key[16],
                        const unsigned char iv[16], const unsigned char *in,
                        int len, unsigned char *out)
{
    int n = 0, f = 0;
    if (EVP_DecryptInit_ex(c, NULL, NULL, key, iv) != 1 ||
        EVP_CIPHER_CTX_set_padding(c, 1) != 1 ||   /* el ctx conserva padding=0 */
        EVP_DecryptUpdate(c, out, &n, in, len) != 1)
        fail("Error al descifrar.");
    if (EVP_DecryptFinal_ex(c, out + n, &f) != 1) {
        ERR_clear_error();
        return -1;
    }
    return n + f;
}

/* M5: prueba una candidata con un solo bloque AES; el resto solo si coincide. */
static int try_key(EVP_CIPHER_CTX *c, const unsigned char prefix[8],
                   uint64_t cand, const unsigned char iv[16],
                   const unsigned char *cipher, int clen,
                   const unsigned char *msg, int mlen, unsigned char *plain)
{
    unsigned char key[16], first[BLOCK + BLOCK];
    int n = 0;
    make_key(prefix, cand, key);

    /* Primer bloque sin relleno (CBC: P1 = D(C1) xor IV). */
    if (EVP_DecryptInit_ex(c, NULL, NULL, key, iv) != 1 ||
        EVP_CIPHER_CTX_set_padding(c, 0) != 1 ||
        EVP_DecryptUpdate(c, first, &n, cipher, BLOCK) != 1)
        fail("Error en la prueba rapida.");
    int cmp = mlen < BLOCK ? mlen : BLOCK;
    if (memcmp(first, msg, (size_t)cmp) != 0)
        return 0;

    int plen = decrypt_full(c, key, iv, cipher, clen, plain);
    return plen == mlen && memcmp(plain, msg, (size_t)mlen) == 0;
}

static uint64_t parse_u64(const char *s, const char *what)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    if (*s == '\0' || *end != '\0') {
        fprintf(stderr, "Valor invalido para %s: %s\n", what, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

int main(int argc, char **argv)
{
    unsigned bits = 20;
    int have_secret = 0;
    uint64_t secret = 0;
    const char *text = "Puedes lograrlo!";

    for (int i = 1; i + 1 < argc; i += 2) {                /* M1, M6 */
        if (!strcmp(argv[i], "-n")) bits = (unsigned)parse_u64(argv[i + 1], "-n");
        else if (!strcmp(argv[i], "-k")) { secret = parse_u64(argv[i + 1], "-k"); have_secret = 1; }
        else if (!strcmp(argv[i], "-m")) text = argv[i + 1];
        else { fprintf(stderr, "Opcion desconocida: %s\n", argv[i]); return EXIT_FAILURE; }
    }
    if ((argc - 1) % 2 != 0 || bits < 1 || bits > 40) {
        fprintf(stderr, "Uso: %s [-n bits(1-40)] [-k secreta] [-m mensaje]\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint64_t total = UINT64_C(1) << bits;
    int mlen = (int)strlen(text);
    if (mlen == 0 || mlen > MAX_MSG) {
        fprintf(stderr, "El mensaje debe tener entre 1 y %d bytes.\n", MAX_MSG);
        return EXIT_FAILURE;
    }

    unsigned char prefix[8], iv[16];
    if (RAND_bytes(prefix, 8) != 1 || RAND_bytes(iv, 16) != 1)
        fail("RAND_bytes fallo.");
    if (!have_secret) {                                      /* M2 */
        if (RAND_bytes((unsigned char *)&secret, 8) != 1) fail("RAND_bytes fallo.");
        secret %= total;
    }
    if (secret >= total) {
        fprintf(stderr, "La clave secreta debe ser < 2^%u.\n", bits);
        return EXIT_FAILURE;
    }

    unsigned char cipher[MAX_MSG + BLOCK], plain[MAX_MSG + BLOCK], key[16];
    make_key(prefix, secret, key);
    int clen = encrypt(key, iv, (const unsigned char *)text, mlen, cipher);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    /* M7: algoritmo/modo se eligen una sola vez; por candidata solo cambia la clave. */
    if (!ctx || EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, NULL, NULL) != 1)
        fail("No se pudo crear el contexto.");

    uint64_t found = UINT64_MAX;
    double start = get_time();
    for (uint64_t k = 0; k < total; k++) {
        if (try_key(ctx, prefix, k, iv, cipher, clen,
                    (const unsigned char *)text, mlen, plain)) {
            found = k;
            break;
        }
    }
    double elapsed = get_time() - start;

    printf("Rango: [0, 2^%u) = %" PRIu64 " candidatas de 2^128\n", bits, total);
    if (found != UINT64_MAX) {
        printf("Clave encontrada: %" PRIu64 "\n", found);
        printf("Mensaje: %.*s\n", mlen, plain);
    } else {
        printf("No se encontro la clave.\n");
    }
    printf("Ejecucion: secuencial\nTiempo: %.6f segundos\n", elapsed);

    EVP_CIPHER_CTX_free(ctx);
    return EXIT_SUCCESS;
}
