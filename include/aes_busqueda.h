/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida
 * aes_busqueda.h - Rutinas compartidas para la busqueda de claves AES-128
 *
 * Funciones comunes a la version secuencial y a las versiones MPI:
 * construccion de la clave, cifrado/descifrado AES-128-CBC, prueba de
 * una candidata, reloj monotono y utilidades de error/argumentos.
 *----------------------------------------------------------------------*/
#ifndef AES_BUSQUEDA_H
#define AES_BUSQUEDA_H

#include <stdint.h>
#include <openssl/evp.h>

#define MAX_MSG 1024   /* largo maximo del mensaje en bytes */
#define BLOCK   16     /* tamano de bloque AES en bytes */

/* Muestra el error (y la cola de errores de OpenSSL) y termina. */
void fail(const char *description);

/* Tiempo de un reloj monotono, en segundos. */
double get_time(void);

/* Convierte s a uint64_t; termina con error si no es un numero valido. */
uint64_t parse_u64(const char *s, const char *what);

/* M4: clave = prefijo (8 B, conocido) || candidata (8 B, big endian). */
void make_key(const unsigned char prefix[8], uint64_t cand,
              unsigned char key[16]);

/* Cifra con AES-128-CBC + PKCS#7. Devuelve la longitud del cifrado. */
int encrypt(const unsigned char key[16], const unsigned char iv[16],
            const unsigned char *in, int len, unsigned char *out);

/* Descifra y valida el relleno. Devuelve la longitud del texto, o -1 si
 * la clave es incorrecta (relleno invalido). */
int decrypt_full(EVP_CIPHER_CTX *c, const unsigned char key[16],
                 const unsigned char iv[16], const unsigned char *in,
                 int len, unsigned char *out);

/* M5: prueba una candidata con un solo bloque AES; el descifrado completo
 * solo ocurre si el primer bloque coincide. Devuelve 1 si es la clave. */
int try_key(EVP_CIPHER_CTX *c, const unsigned char prefix[8],
            uint64_t cand, const unsigned char iv[16],
            const unsigned char *cipher, int clen,
            const unsigned char *msg, int mlen, unsigned char *plain);

#endif /* AES_BUSQUEDA_H */
