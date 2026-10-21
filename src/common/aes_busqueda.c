/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida
 * aes_busqueda.c - Implementacion de las rutinas compartidas
 *                  (ver include/aes_busqueda.h)
 *----------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L

#include "aes_busqueda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/err.h>

void fail(const char *d)
{
    fprintf(stderr, "%s\n", d);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

double get_time(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("reloj");
        exit(EXIT_FAILURE);
    }
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

uint64_t parse_u64(const char *s, const char *what)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    if (*s == '\0' || *end != '\0') {
        fprintf(stderr, "Valor invalido para %s: %s\n", what, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

void make_key(const unsigned char prefix[8], uint64_t cand,
              unsigned char key[16])
{
    memcpy(key, prefix, 8);
    for (int i = 0; i < 8; i++)
        key[15 - i] = (unsigned char)(cand >> (8 * i));
}

int encrypt(const unsigned char key[16], const unsigned char iv[16],
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

int decrypt_full(EVP_CIPHER_CTX *c, const unsigned char key[16],
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

int try_key(EVP_CIPHER_CTX *c, const unsigned char prefix[8],
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
