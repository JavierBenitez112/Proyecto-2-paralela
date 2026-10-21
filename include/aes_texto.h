/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Proyecto 02
 * aes_texto.h - Rutinas compartidas del Proyecto 02
 *
 * Cifrado/descifrado AES-128-CBC de textos leidos desde archivo, conversion
 * de una clave numerica a 128 bits, prueba de una candidata con la frase de
 * busqueda y calculo del reparto de claves para cada estrategia MPI.
 *
 * Las usan los tres programas de src/proyecto02/:
 *   aes_texto            cifrar / descifrar un .txt con una clave dada
 *   busqueda_secuencial  fuerza bruta con un solo proceso (referencia)
 *   busqueda_mpi         fuerza bruta con Open MPI (estrategias 1 y 2)
 *
 * Formato del archivo cifrado (.bin): IV (16 B) || texto cifrado (n*16 B).
 *----------------------------------------------------------------------*/
#ifndef AES_TEXTO_H
#define AES_TEXTO_H

#include <stddef.h>
#include <stdint.h>
#include <openssl/evp.h>

#define AES_KEY_LEN    16          /* AES-128: clave de 16 bytes          */
#define AES_BLOCK_LEN  16          /* tamano de bloque AES                */
#define AES_IV_LEN     16          /* vector de inicializacion de CBC     */
#define MAX_TEXTO      (1u << 20)  /* 1 MiB: limite de lectura de archivo */
#define MAX_BITS_RANGO 62          /* rango [0, 2^bits) con bits <= 62    */

/* Las longitudes se pasan a OpenSSL como int: el maximo debe caber. */
_Static_assert(MAX_TEXTO + 2 * 16 <= 0x7fffffff, "MAX_TEXTO no cabe en int");

/* Estrategias de reparto del rango de claves entre procesos. */
enum estrategia {
    ESTR_INTERCALADA = 1,   /* base: k = r, r+p, r+2p, ...           */
    ESTR_BLOQUES     = 2    /* alternativa: bloque contiguo por rank */
};

/* Implementacion de AES usada para probar candidatas (opcion -a). */
enum motor_aes {
    MOTOR_AUTO  = 0,        /* AES-NI si la CPU lo tiene, si no EVP  */
    MOTOR_EVP   = 1,        /* API EVP de OpenSSL (portable)         */
    MOTOR_AESNI = 2         /* instrucciones AES-NI de x86 (rapido)  */
};

/* Bloque de memoria dinamica con su longitud (liberar con buffer_liberar). */
typedef struct {
    unsigned char *datos;
    size_t len;
} buffer_t;

/* Tramo de claves de un proceso: candidatas inicio + i*paso, 0 <= i < cantidad. */
typedef struct {
    uint64_t inicio;
    uint64_t paso;
    uint64_t cantidad;
} tramo_t;

/* Estado reutilizable para probar candidatas (uno por proceso). */
typedef struct {
    enum motor_aes motor;          /* MOTOR_EVP o MOTOR_AESNI (ya resuelto) */
    EVP_CIPHER_CTX *ctx;           /* contexto EVP (solo con MOTOR_EVP)    */
    unsigned char clave[AES_KEY_LEN]; /* clave de trabajo: bytes 0..7 = 0  */
    const unsigned char *iv;       /* IV del mensaje cifrado               */
    const unsigned char *cifrado;  /* texto cifrado (sin el IV)            */
    int clen;                      /* longitud del cifrado (multiplo de 16) */
    const unsigned char *frase;    /* frase de busqueda                    */
    size_t flen;
    unsigned char *plano;          /* buffer de trabajo de clen bytes      */
} buscador_t;

/* ---------------------------------------------------------- utilidades */

/* Imprime el mensaje (y la cola de errores de OpenSSL) en stderr. */
void reportar_error(const char *mensaje);

/* Reloj monotono, en segundos. */
double reloj(void);

/* Convierte una cadena decimal (o hexadecimal con prefijo 0x) a uint64_t.
 * Devuelve 0 si es valida, -1 si no (vacia, con basura o desbordada). */
int parse_u64_seguro(const char *s, uint64_t *valor);

/* Convierte 32 digitos hexadecimales en una clave de 16 bytes. 0 / -1. */
int parse_clave_hex(const char *s, unsigned char clave[AES_KEY_LEN]);

/* Escribe los bytes en hexadecimal (sin salto de linea). */
void imprimir_hex(const unsigned char *d, size_t n);

/* Escribe el texto mostrando los bytes no imprimibles como \xNN. */
void imprimir_texto(const unsigned char *d, size_t n);

/* ---------------------------------------------------------- archivos */

/* Lee el archivo completo (hasta max bytes). 0 si todo bien, -1 si falla
 * (inexistente, vacio, demasiado grande o error de lectura). */
int leer_archivo(const char *ruta, size_t max, buffer_t *out);

/* Escribe n bytes en el archivo (lo crea o reemplaza). 0 / -1. */
int escribir_archivo(const char *ruta, const unsigned char *d, size_t n);

/* Libera el buffer y lo deja vacio (seguro llamarla dos veces). */
void buffer_liberar(buffer_t *b);

/* ---------------------------------------------------------- AES-128 */

/* Clave numerica -> 128 bits: 16 bytes big endian con ceros a la izquierda.
 * Ej.: 123456 -> 00000000 00000000 00000000 0001E240. */
void clave_desde_u64(uint64_t k, unsigned char clave[AES_KEY_LEN]);

/* AES-128-CBC + PKCS#7. out debe tener len + 16 bytes. Devuelve la
 * longitud del cifrado o -1 si OpenSSL falla. */
int cifrar_texto(const unsigned char clave[AES_KEY_LEN],
                 const unsigned char iv[AES_IV_LEN],
                 const unsigned char *in, int len, unsigned char *out);

/* Descifra y valida el relleno PKCS#7. out debe tener len bytes.
 * Devuelve la longitud del texto, o -1 si la clave es incorrecta
 * (relleno invalido) o el cifrado esta mal formado. */
int descifrar_texto(const unsigned char clave[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *in, int len, unsigned char *out);

/* ---------------------------------------------------------- busqueda */

/* 1 si la frase aparece en el texto (busqueda en cualquier posicion). */
int contiene_frase(const unsigned char *texto, size_t tlen,
                   const unsigned char *frase, size_t flen);

/* 1 si este programa y esta CPU pueden usar las instrucciones AES-NI. */
int aesni_disponible(void);

/* Prepara el buscador con el motor pedido: con EVP crea el contexto
 * (AES-128-CBC sin relleno) una sola vez; con AUTO elige AES-NI si esta
 * disponible. Reserva el buffer de trabajo. 0 / -1 (p. ej. si se pide
 * AES-NI y la CPU no lo tiene). */
int buscador_iniciar(buscador_t *b, const unsigned char *iv,
                     const unsigned char *cifrado, int clen,
                     const unsigned char *frase, size_t flen,
                     enum motor_aes motor);

/* Prueba la candidata k: descifra TODO el mensaje con la clave de 128 bits
 * de k (como clave_desde_u64) y revisa si contiene la frase en cualquier
 * posicion. 1 si la contiene, 0 si no, -1 si OpenSSL falla. */
int probar_clave(buscador_t *b, uint64_t k);

/* Libera el contexto y el buffer del buscador. */
void buscador_liberar(buscador_t *b);

/* Nombre legible del motor. */
const char *nombre_motor(enum motor_aes m);

/* ---------------------------------------------------------- reparto */

/* Tramo del proceso rank (de p) en el rango [0, n) segun la estrategia.
 * Las p partes cubren el rango sin omitir ni repetir candidatas. */
tramo_t calcular_tramo(enum estrategia e, uint64_t n, int p, int rank);

/* Para la clave k: que proceso la tiene y cuantos intentos le toma
 * (incluyendo el de k). Sirve para estimar tiempos. */
void ubicar_clave(enum estrategia e, uint64_t n, int p, uint64_t k,
                  int *rank, uint64_t *intentos);

/* Nombre legible de la estrategia. */
const char *nombre_estrategia(enum estrategia e);

/* ---------------------------------------------------------- programas de busqueda */

#define FRASE_DEFECTO "es una prueba de"
#define BITS_DEFECTO  56
#define INTERVALO_REVISION 4096   /* candidatas entre revisiones de aviso/tiempo */

/* Opciones de linea de comandos comunes a busqueda_secuencial y busqueda_mpi:
 *   -i texto.txt -k CLAVE   cifra el texto con CLAVE (solo para preparar la
 *                           prueba; la busqueda no usa CLAVE), o bien
 *   -c cifrado.bin          archivo generado por aes_texto -e
 *   -f frase                frase de busqueda          (def. "es una prueba de")
 *   -n bits                 rango [0, 2^bits)           (def. 56, max. 62)
 *   -t segundos             limite de tiempo (0 = sin limite)
 *   -e 1|2                  estrategia (solo busqueda_mpi; def. 1)
 *   -a auto|evp|aesni       implementacion de AES (def. auto)          */
typedef struct {
    const char *texto;
    const char *clave;
    const char *cifrado;
    const char *frase;
    unsigned bits;
    double limite;
    int estrategia;
    enum motor_aes motor;
} opciones_t;

/* Llena o con los argumentos. Devuelve 0, o -1 con el motivo en error. */
int parse_opciones(int argc, char **argv, int con_estrategia,
                   opciones_t *o, char *error, size_t error_len);

/* Mensaje cifrado listo para buscar. */
typedef struct {
    unsigned char iv[AES_IV_LEN];
    buffer_t cifrado;          /* sin el IV                                 */
    int tiene_referencia;      /* 1 si se cifro aqui con -k (dato conocido) */
    uint64_t clave_referencia; /* solo para reportar/estimar, nunca se usa
                                  para buscar                               */
} mensaje_t;

/* Obtiene el mensaje cifrado segun las opciones (-i/-k o -c). 0 / -1. */
int preparar_mensaje(const opciones_t *o, mensaje_t *m);

void mensaje_liberar(mensaje_t *m);

/* Imprime una duracion en s y, si es grande, en horas/dias/anios. */
void imprimir_duracion(double segundos);

#endif /* AES_TEXTO_H */
