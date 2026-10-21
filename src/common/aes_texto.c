/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Proyecto 02
 * aes_texto.c - Implementacion de las rutinas compartidas del Proyecto 02
 *               (documentacion de cada funcion en include/aes_texto.h)
 *
 * Programacion defensiva: ninguna rutina termina el programa; todas
 * devuelven un codigo de error para que el llamador (secuencial o MPI)
 * libere recursos y salga de forma ordenada (en MPI, sin dejar procesos
 * colgados).
 *----------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L

#include "aes_texto.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/err.h>
#include <openssl/rand.h>

/* ================================================================ utilidades */

void reportar_error(const char *mensaje)
{
    fprintf(stderr, "Error: %s\n", mensaje);
    ERR_print_errors_fp(stderr);
}

double reloj(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
        return 0.0;
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int parse_u64_seguro(const char *s, uint64_t *valor)
{
    if (s == NULL || *s == '\0' || *s == '-' || isspace((unsigned char)*s))
        return -1;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == '\0')
            return -1;
    }
    char *fin = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &fin, base);
    if (errno == ERANGE || fin == s || *fin != '\0')
        return -1;
    *valor = (uint64_t)v;
    return 0;
}

static int valor_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int parse_clave_hex(const char *s, unsigned char clave[AES_KEY_LEN])
{
    if (s == NULL || strlen(s) != 2 * AES_KEY_LEN)
        return -1;
    for (int i = 0; i < AES_KEY_LEN; i++) {
        int hi = valor_hex(s[2 * i]), lo = valor_hex(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        clave[i] = (unsigned char)(hi << 4 | lo);
    }
    return 0;
}

void imprimir_hex(const unsigned char *d, size_t n)
{
    for (size_t i = 0; i < n; i++)
        printf("%02x", d[i]);
}

void imprimir_texto(const unsigned char *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (d[i] == '\n')
            fputs("\\n", stdout);
        else if (d[i] == '\r')
            fputs("\\r", stdout);
        else if (isprint(d[i]) || d[i] >= 0x80)   /* deja pasar UTF-8 */
            putchar(d[i]);
        else
            printf("\\x%02x", d[i]);
    }
}

/* ================================================================ archivos */

int leer_archivo(const char *ruta, size_t max, buffer_t *out)
{
    out->datos = NULL;
    out->len = 0;

    FILE *f = fopen(ruta, "rb");
    if (f == NULL) {
        fprintf(stderr, "Error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        return -1;
    }

    /* Se lee por partes en vez de confiar en fseek/ftell: funciona igual
     * con archivos normales, tuberias y /dev/stdin. */
    size_t cap = 4096, len = 0;
    unsigned char *datos = malloc(cap);
    if (datos == NULL) {
        fclose(f);
        reportar_error("memoria insuficiente al leer el archivo");
        return -1;
    }
    for (;;) {
        if (len == cap) {
            if (cap > max) break;                      /* ya se paso del limite */
            size_t ncap = cap * 2;
            unsigned char *tmp = realloc(datos, ncap);
            if (tmp == NULL) {
                free(datos);
                fclose(f);
                reportar_error("memoria insuficiente al leer el archivo");
                return -1;
            }
            datos = tmp;
            cap = ncap;
        }
        size_t r = fread(datos + len, 1, cap - len, f);
        len += r;
        if (r == 0) break;
    }
    int error_lectura = ferror(f);
    fclose(f);

    if (error_lectura) {
        free(datos);
        fprintf(stderr, "Error: fallo la lectura de '%s'\n", ruta);
        return -1;
    }
    if (len == 0) {
        free(datos);
        fprintf(stderr, "Error: el archivo '%s' esta vacio\n", ruta);
        return -1;
    }
    if (len > max) {
        free(datos);
        fprintf(stderr, "Error: '%s' supera el maximo de %zu bytes\n", ruta, max);
        return -1;
    }
    out->datos = datos;
    out->len = len;
    return 0;
}

int escribir_archivo(const char *ruta, const unsigned char *d, size_t n)
{
    FILE *f = fopen(ruta, "wb");
    if (f == NULL) {
        fprintf(stderr, "Error: no se pudo crear '%s': %s\n", ruta, strerror(errno));
        return -1;
    }
    size_t w = fwrite(d, 1, n, f);
    int cierre = fclose(f);
    if (w != n || cierre != 0) {
        fprintf(stderr, "Error: no se pudo escribir '%s'\n", ruta);
        return -1;
    }
    return 0;
}

void buffer_liberar(buffer_t *b)
{
    if (b == NULL) return;
    free(b->datos);
    b->datos = NULL;
    b->len = 0;
}

/* ================================================================ AES-128 */

void clave_desde_u64(uint64_t k, unsigned char clave[AES_KEY_LEN])
{
    memset(clave, 0, AES_KEY_LEN);                 /* bytes 0..7 = 0     */
    for (int i = 0; i < 8; i++)                    /* bytes 8..15 = k BE */
        clave[AES_KEY_LEN - 1 - i] = (unsigned char)(k >> (8 * i));
}

int cifrar_texto(const unsigned char clave[AES_KEY_LEN],
                 const unsigned char iv[AES_IV_LEN],
                 const unsigned char *in, int len, unsigned char *out)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0, f = 0, ok = 0;
    if (c != NULL &&
        EVP_EncryptInit_ex(c, EVP_aes_128_cbc(), NULL, clave, iv) == 1 &&
        EVP_EncryptUpdate(c, out, &n, in, len) == 1 &&
        EVP_EncryptFinal_ex(c, out + n, &f) == 1)
        ok = 1;
    EVP_CIPHER_CTX_free(c);                        /* acepta NULL */
    if (!ok) {
        reportar_error("fallo el cifrado AES-128-CBC");
        return -1;
    }
    return n + f;
}

int descifrar_texto(const unsigned char clave[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *in, int len, unsigned char *out)
{
    if (len <= 0 || len % AES_BLOCK_LEN != 0)
        return -1;                                 /* cifrado mal formado */
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0, f = 0, ok = 0;
    if (c != NULL &&
        EVP_DecryptInit_ex(c, EVP_aes_128_cbc(), NULL, clave, iv) == 1 &&
        EVP_DecryptUpdate(c, out, &n, in, len) == 1 &&
        EVP_DecryptFinal_ex(c, out + n, &f) == 1)  /* valida PKCS#7 */
        ok = 1;
    EVP_CIPHER_CTX_free(c);
    ERR_clear_error();          /* un relleno invalido no es un error fatal */
    return ok ? n + f : -1;
}

/* ================================================================ busqueda */

int contiene_frase(const unsigned char *texto, size_t tlen,
                   const unsigned char *frase, size_t flen)
{
    if (flen == 0) return 1;
    if (flen > tlen) return 0;
    const unsigned char *p = texto, *ultimo = texto + (tlen - flen);
    while (p <= ultimo) {
        /* memchr salta rapido hasta la siguiente aparicion del 1er byte */
        p = memchr(p, frase[0], (size_t)(ultimo - p) + 1);
        if (p == NULL) return 0;
        if (memcmp(p, frase, flen) == 0) return 1;
        p++;
    }
    return 0;
}

/* ---------------------------------------------------------------- AES-NI
 * Ruta rapida: expansion de clave y descifrado CBC con las instrucciones
 * AES-NI de x86 (intrinsics). Evita el costo por llamada de EVP, que en
 * OpenSSL 3 reinicia el contexto del proveedor en cada clave; se mide
 * ~2.7x mas rapida (Tabla 7 del informe). Las funciones llevan el atributo target
 * para no exigir -maes en el Makefile, y solo se llaman si la CPU tiene
 * AES-NI (aesni_disponible). En otras arquitecturas se usa solo EVP. */
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#  include <immintrin.h>
#  define HAY_AESNI 1
#  define CON_AESNI __attribute__((target("aes,sse4.1")))
#else
#  define HAY_AESNI 0
#endif

#if HAY_AESNI
/* Un paso de la expansion de clave de AES-128 (FIPS 197, KeyExpansion). */
static inline CON_AESNI __m128i expandir_paso(__m128i k, __m128i gen)
{
    gen = _mm_shuffle_epi32(gen, _MM_SHUFFLE(3, 3, 3, 3));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    return _mm_xor_si128(k, gen);
}

/* 11 subclaves de descifrado ("cifrado inverso equivalente"):
 * dk[0] = rk10, dk[i] = InvMixColumns(rk[10-i]) para 1..9, dk[10] = rk0. */
static inline CON_AESNI void subclaves_descifrado(const unsigned char clave[AES_KEY_LEN],
                                                  __m128i dk[11])
{
    __m128i rk[11];
    rk[0]  = _mm_loadu_si128((const __m128i *)clave);
    rk[1]  = expandir_paso(rk[0], _mm_aeskeygenassist_si128(rk[0], 0x01));
    rk[2]  = expandir_paso(rk[1], _mm_aeskeygenassist_si128(rk[1], 0x02));
    rk[3]  = expandir_paso(rk[2], _mm_aeskeygenassist_si128(rk[2], 0x04));
    rk[4]  = expandir_paso(rk[3], _mm_aeskeygenassist_si128(rk[3], 0x08));
    rk[5]  = expandir_paso(rk[4], _mm_aeskeygenassist_si128(rk[4], 0x10));
    rk[6]  = expandir_paso(rk[5], _mm_aeskeygenassist_si128(rk[5], 0x20));
    rk[7]  = expandir_paso(rk[6], _mm_aeskeygenassist_si128(rk[6], 0x40));
    rk[8]  = expandir_paso(rk[7], _mm_aeskeygenassist_si128(rk[7], 0x80));
    rk[9]  = expandir_paso(rk[8], _mm_aeskeygenassist_si128(rk[8], 0x1b));
    rk[10] = expandir_paso(rk[9], _mm_aeskeygenassist_si128(rk[9], 0x36));
    dk[0] = rk[10];
    for (int i = 1; i < 10; i++)
        dk[i] = _mm_aesimc_si128(rk[10 - i]);
    dk[10] = rk[0];
}

/* Descifra AES-128-CBC sin relleno: P_i = D(C_i) xor C_(i-1), C_(-1) = IV. */
static CON_AESNI __attribute__((noinline))
void descifrar_cbc_aesni(const unsigned char clave[AES_KEY_LEN],
                         const unsigned char *iv, const unsigned char *in,
                         int len, unsigned char *out)
{
    __m128i dk[11];
    subclaves_descifrado(clave, dk);
    __m128i previo = _mm_loadu_si128((const __m128i *)iv);
    for (int off = 0; off < len; off += AES_BLOCK_LEN) {
        __m128i c = _mm_loadu_si128((const __m128i *)(in + off));
        __m128i s = _mm_xor_si128(c, dk[0]);
        for (int r = 1; r < 10; r++)
            s = _mm_aesdec_si128(s, dk[r]);
        s = _mm_aesdeclast_si128(s, dk[10]);
        _mm_storeu_si128((__m128i *)(out + off), _mm_xor_si128(s, previo));
        previo = c;
    }
}
#endif /* HAY_AESNI */

int aesni_disponible(void)
{
#if HAY_AESNI
    __builtin_cpu_init();
    return __builtin_cpu_supports("aes") && __builtin_cpu_supports("sse4.1");
#else
    return 0;
#endif
}

const char *nombre_motor(enum motor_aes m)
{
    switch (m) {
    case MOTOR_EVP:   return "OpenSSL EVP";
    case MOTOR_AESNI: return "AES-NI (instrucciones x86)";
    default:          return "auto";
    }
}

/* ---------------------------------------------------------------- buscador */

/* 1 si el ultimo bloque termina con un relleno PKCS#7 valido. El cifrado
 * de aes_texto siempre lo tiene, asi que la clave correcta siempre pasa;
 * sirve para descartar claves incorrectas cuyo texto contenga la frase por
 * azar (falsos positivos, probables con frases muy cortas). */
static int relleno_valido(const unsigned char *t, int len)
{
    unsigned pad = t[len - 1];
    if (pad == 0 || pad > AES_BLOCK_LEN)
        return 0;
    for (unsigned i = 1; i <= pad; i++)
        if (t[len - (int)i] != pad)
            return 0;
    return 1;
}

int buscador_iniciar(buscador_t *b, const unsigned char *iv,
                     const unsigned char *cifrado, int clen,
                     const unsigned char *frase, size_t flen,
                     enum motor_aes motor)
{
    memset(b, 0, sizeof *b);
    if (clen <= 0 || clen % AES_BLOCK_LEN != 0) {
        reportar_error("el texto cifrado debe medir un multiplo de 16 bytes");
        return -1;
    }
    if (motor == MOTOR_AUTO)
        motor = aesni_disponible() ? MOTOR_AESNI : MOTOR_EVP;
    if (motor == MOTOR_AESNI && !aesni_disponible()) {
        reportar_error("esta CPU (o este compilador) no tiene AES-NI; use -a evp");
        return -1;
    }
    b->motor = motor;
    b->plano = malloc((size_t)clen);
    if (b->plano == NULL) {
        reportar_error("memoria insuficiente para el buffer de busqueda");
        return -1;
    }
    if (motor == MOTOR_EVP) {
        /* El algoritmo, el modo y "sin relleno" se fijan una sola vez; por
         * candidata solo cambia la clave (mejora M7 del Laboratorio 03). */
        b->ctx = EVP_CIPHER_CTX_new();
        if (b->ctx == NULL ||
            EVP_DecryptInit_ex(b->ctx, EVP_aes_128_cbc(), NULL, NULL, NULL) != 1 ||
            EVP_CIPHER_CTX_set_padding(b->ctx, 0) != 1) {
            reportar_error("no se pudo preparar el contexto de busqueda");
            buscador_liberar(b);
            return -1;
        }
    }
    b->iv = iv;
    b->cifrado = cifrado;
    b->clen = clen;
    b->frase = frase;
    b->flen = flen;
    return 0;                      /* b->clave ya quedo en cero (memset) */
}

int probar_clave(buscador_t *b, uint64_t k)
{
    /* Igual que clave_desde_u64, pero los bytes 0..7 ya son cero desde
     * buscador_iniciar: solo se escriben los 8 bytes bajos (big endian). */
    for (int i = 0; i < 8; i++)
        b->clave[AES_KEY_LEN - 1 - i] = (unsigned char)(k >> (8 * i));

    /* Se descifra todo el mensaje sin validar el relleno: con una clave
     * incorrecta el ultimo bloque es basura y no debe tratarse como error;
     * lo que decide es la frase, que puede estar en cualquier posicion. */
#if HAY_AESNI
    if (b->motor == MOTOR_AESNI) {
        descifrar_cbc_aesni(b->clave, b->iv, b->cifrado, b->clen, b->plano);
    } else
#endif
    {
        int n = 0;
        if (EVP_DecryptInit_ex(b->ctx, NULL, NULL, b->clave, b->iv) != 1 ||
            EVP_DecryptUpdate(b->ctx, b->plano, &n, b->cifrado, b->clen) != 1 ||
            n != b->clen)
            return -1;
    }
    /* La frase decide; el relleno solo se revisa en el caso raro de que
     * aparezca, asi que no cuesta nada en las candidatas incorrectas. */
    return contiene_frase(b->plano, (size_t)b->clen, b->frase, b->flen) &&
           relleno_valido(b->plano, b->clen);
}

void buscador_liberar(buscador_t *b)
{
    if (b == NULL) return;
    EVP_CIPHER_CTX_free(b->ctx);           /* acepta NULL */
    free(b->plano);
    b->ctx = NULL;
    b->plano = NULL;
}

/* ================================================================ reparto */

tramo_t calcular_tramo(enum estrategia e, uint64_t n, int p, int rank)
{
    tramo_t t = {0, 1, 0};
    uint64_t up = (uint64_t)p, ur = (uint64_t)rank;

    if (e == ESTR_INTERCALADA) {
        /* El proceso r prueba r, r+p, r+2p, ... (k mod p == r). */
        t.inicio = ur;
        t.paso = up;
        t.cantidad = ur < n ? (n - ur + up - 1) / up : 0;
    } else {
        /* Bloques contiguos; los primeros (n mod p) procesos llevan una
         * clave extra para no omitir el residuo. */
        uint64_t tam = n / up, resto = n % up;
        t.inicio = ur * tam + (ur < resto ? ur : resto);
        t.paso = 1;
        t.cantidad = tam + (ur < resto ? 1 : 0);
    }
    return t;
}

void ubicar_clave(enum estrategia e, uint64_t n, int p, uint64_t k,
                  int *rank, uint64_t *intentos)
{
    uint64_t up = (uint64_t)p;
    if (e == ESTR_INTERCALADA) {
        *rank = (int)(k % up);
        *intentos = k / up + 1;
        return;
    }
    uint64_t tam = n / up, resto = n % up;
    uint64_t corte = resto * (tam + 1);       /* claves en bloques "grandes" */
    uint64_t r, off;
    if (k < corte) {
        r = k / (tam + 1);
        off = k % (tam + 1);
    } else {
        r = resto + (k - corte) / tam;        /* tam > 0 si k < n y k >= corte */
        off = (k - corte) % tam;
    }
    *rank = (int)r;
    *intentos = off + 1;
}

const char *nombre_estrategia(enum estrategia e)
{
    return e == ESTR_INTERCALADA ? "1 - reparto intercalado (base)"
                                 : "2 - bloques contiguos (alternativa)";
}

/* ================================================================ programas de busqueda */

int parse_opciones(int argc, char **argv, int con_estrategia,
                   opciones_t *o, char *error, size_t error_len)
{
    o->texto = o->clave = o->cifrado = NULL;
    o->frase = FRASE_DEFECTO;
    o->bits = BITS_DEFECTO;
    o->limite = 0.0;
    o->estrategia = ESTR_INTERCALADA;
    o->motor = MOTOR_AUTO;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0' || a[2] != '\0') {
            snprintf(error, error_len, "argumento inesperado '%s'", a);
            return -1;
        }
        if (strchr("ikcfntea", a[1]) == NULL) {
            snprintf(error, error_len, "opcion desconocida '%s'", a);
            return -1;
        }
        if (i + 1 >= argc) {
            snprintf(error, error_len, "falta el valor de %s", a);
            return -1;
        }
        const char *v = argv[++i];
        uint64_t n;
        char *fin;
        switch (a[1]) {
        case 'i': o->texto = v; break;
        case 'k': o->clave = v; break;
        case 'c': o->cifrado = v; break;
        case 'f': o->frase = v; break;
        case 'n':
            if (parse_u64_seguro(v, &n) != 0 || n < 1 || n > MAX_BITS_RANGO) {
                snprintf(error, error_len, "-n debe estar entre 1 y %d", MAX_BITS_RANGO);
                return -1;
            }
            o->bits = (unsigned)n;
            break;
        case 't':
            o->limite = strtod(v, &fin);
            if (*v == '\0' || *fin != '\0' || !isfinite(o->limite) || o->limite < 0.0) {
                snprintf(error, error_len, "-t debe ser un numero de segundos >= 0");
                return -1;
            }
            break;
        case 'e':
            if (!con_estrategia || parse_u64_seguro(v, &n) != 0 || (n != 1 && n != 2)) {
                snprintf(error, error_len, con_estrategia ? "-e debe ser 1 o 2"
                                                          : "opcion -e no disponible");
                return -1;
            }
            o->estrategia = (int)n;
            break;
        case 'a':
            if (strcmp(v, "auto") == 0)       o->motor = MOTOR_AUTO;
            else if (strcmp(v, "evp") == 0)   o->motor = MOTOR_EVP;
            else if (strcmp(v, "aesni") == 0) o->motor = MOTOR_AESNI;
            else {
                snprintf(error, error_len, "-a debe ser auto, evp o aesni");
                return -1;
            }
            break;
        default:
            snprintf(error, error_len, "opcion desconocida '%s'", a);
            return -1;
        }
    }
    if ((o->cifrado != NULL) == (o->texto != NULL)) {
        snprintf(error, error_len, "use -i texto.txt -k CLAVE, o bien -c cifrado.bin");
        return -1;
    }
    if (o->texto != NULL && o->clave == NULL) {
        snprintf(error, error_len, "-i requiere -k CLAVE para preparar el cifrado");
        return -1;
    }
    if (o->cifrado != NULL && o->clave != NULL) {
        snprintf(error, error_len, "con -c no se pasa -k (la clave es la que se busca)");
        return -1;
    }
    size_t flen = strlen(o->frase);
    if (flen == 0 || flen > 256) {
        snprintf(error, error_len, "la frase de busqueda debe tener entre 1 y 256 bytes");
        return -1;
    }
    return 0;
}

int preparar_mensaje(const opciones_t *o, mensaje_t *m)
{
    memset(m, 0, sizeof *m);

    if (o->cifrado != NULL) {                    /* archivo de aes_texto -e */
        buffer_t a;
        if (leer_archivo(o->cifrado, MAX_TEXTO + AES_IV_LEN + AES_BLOCK_LEN, &a) != 0)
            return -1;
        if (a.len < AES_IV_LEN + AES_BLOCK_LEN || (a.len - AES_IV_LEN) % AES_BLOCK_LEN) {
            fprintf(stderr, "Error: '%s' no tiene el formato IV + cifrado\n", o->cifrado);
            buffer_liberar(&a);
            return -1;
        }
        memcpy(m->iv, a.datos, AES_IV_LEN);
        memmove(a.datos, a.datos + AES_IV_LEN, a.len - AES_IV_LEN);
        a.len -= AES_IV_LEN;
        m->cifrado = a;
        return 0;
    }

    /* -i texto -k clave: se cifra aqui (como haria la "victima"). */
    if (parse_u64_seguro(o->clave, &m->clave_referencia) != 0) {
        fprintf(stderr, "Error: clave invalida '%s'\n", o->clave);
        return -1;
    }
    buffer_t texto;
    if (leer_archivo(o->texto, MAX_TEXTO, &texto) != 0)
        return -1;
    unsigned char clave[AES_KEY_LEN];
    clave_desde_u64(m->clave_referencia, clave);
    m->cifrado.datos = malloc(texto.len + AES_BLOCK_LEN);
    int clen = -1;
    if (m->cifrado.datos == NULL)
        reportar_error("memoria insuficiente");
    else if (RAND_bytes(m->iv, AES_IV_LEN) != 1)
        reportar_error("RAND_bytes no pudo generar el IV");
    else
        clen = cifrar_texto(clave, m->iv, texto.datos, (int)texto.len, m->cifrado.datos);
    buffer_liberar(&texto);
    if (clen < 0) {
        mensaje_liberar(m);
        return -1;
    }
    m->cifrado.len = (size_t)clen;
    m->tiene_referencia = 1;
    if (m->clave_referencia >> o->bits != 0)
        fprintf(stderr, "Aviso: la clave %" PRIu64 " esta fuera del rango [0, 2^%u); "
                        "la busqueda no la va a encontrar\n", m->clave_referencia, o->bits);
    return 0;
}

void mensaje_liberar(mensaje_t *m)
{
    if (m != NULL) buffer_liberar(&m->cifrado);
}

void imprimir_duracion(double s)
{
    printf("%.6f s", s);
    if (s >= 365.25 * 86400.0)      printf(" (~%.3g anios)", s / (365.25 * 86400.0));
    else if (s >= 86400.0)          printf(" (~%.2f dias)", s / 86400.0);
    else if (s >= 3600.0)           printf(" (~%.2f horas)", s / 3600.0);
}
