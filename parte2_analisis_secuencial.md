# Parte 2 – Análisis del programa secuencial (ejercicios 5–7)

**Integrante:** Javier Benitez

## 5. ¿El programa usa correctamente AES-128?

| Aspecto | Teoría (Parte 1) | `busqueda_clave_aes_secuencial.c` | ¿Correcto? |
|---|---|---|---|
| Tamaño de clave | 128 bits = 16 bytes | `key[16]` + `EVP_aes_128_ecb()` | Sí, formalmente |
| Bloque | 128 bits = 16 bytes | `MESSAGE_LEN = 16`, un solo bloque, `set_padding(0)` | Sí, pero solo porque el mensaje mide exactamente 16 B |
| Cifrar / descifrar | misma clave, transformación inversa | `crypt_block(..., encrypt=1/0)` con la misma clave derivada de la candidata | Sí |
| Entropía de la clave | 2^128 claves posibles | `make_key` rellena con ceros: solo los 8 bytes bajos varían (y el rango es 2^20) | **No**: es AES-128 con ~20 bits efectivos |

**Conclusión:** el uso de la API es correcto (AES-128, bloque de 16 B, mismo algoritmo para cifrar y descifrar), pero la clave **no tiene 128 bits de seguridad**: 15 de los 16 bytes son cero en la práctica (`key[0..7] = 0`, y de los 8 bajos solo se usan 20 bits). El cifrado es AES-128 de verdad; la clave es débil por construcción, lo cual es intencional (ejercicio educativo de fuerza bruta).

## 6. Ejecución, tiempo y errores conceptuales

### Compilación y ejecución (Linux/WSL – tomar las capturas aquí)

```bash
sudo apt update
sudo apt install libssl-dev
gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_secuencial.c -o busqueda_clave_aes_secuencial -lcrypto
./busqueda_clave_aes_secuencial
```

El programa ya imprime `Tiempo: X segundos` (reloj `CLOCK_MONOTONIC`, medido solo sobre la búsqueda). Capturas en `capturas/03_compilar_original.png` (compilación) y `capturas/04_ejecutar_original.png` (ejecución).

5 corridas del original (`SECRET_KEY = 12345`, al principio del rango):

| Corrida | Tiempo (s) |
|---|---|
| 1 | 0.005666 |
| 2 | 0.005137 |
| 3 | 0.004722 |
| 4 | 0.004776 |
| 5 | 0.004461 |
| **Promedio** | **0.004952** |

El tiempo es bajo y muy estable porque la clave 12345 está pegada al inicio del espacio de búsqueda (candidata #12345 de 1 048 576): el programa casi no recorre nada, así que este tiempo **no sirve para medir Speedup** de forma justa (ver mejora M1/M2 más abajo, y la nota al final de la sección 7).

### Errores conceptuales y limitaciones

1. **Construcción de la clave.** `make_key` es determinista y pone ceros a la izquierda: cualquiera que lea el código conoce 8 de los 16 bytes, y los otros 8 solo toman 2^20 valores. No se genera con un CSPRNG ni se deriva de una contraseña con KDF (PBKDF2/Argon2). `SECRET_KEY = 12345` es una constante en el código, y además queda **al inicio** del rango, por lo que la búsqueda termina casi de inmediato y el tiempo no es representativo (ni sirve para medir Speedup).
2. **Modo ECB.** Bloques de texto iguales producen bloques cifrados iguales, así que filtra patrones y no oculta la estructura del mensaje; no usa IV ni nonce, por lo que el mismo mensaje con la misma clave siempre da el mismo cifrado. Aquí pasa desapercibido porque hay un solo bloque, pero no escala a mensajes de más de 16 B y no ofrece integridad. Además `set_padding(0)` obliga a que el mensaje sea múltiplo exacto de 16 B.
3. **Verificación con el texto original.** El atacante necesita conocer **todo** el texto plano para comparar (`memcmp` de 16 B). En un ataque real normalmente solo se conoce un fragmento (cabecera, formato) o se valida con el relleno; aquí se supone un escenario de "texto plano conocido" completo. Tampoco hay forma de distinguir falsos positivos más allá de la comparación total.
4. **Espacio completo vs. rango explorado.** AES-128 tiene 2^128 ≈ 3.4×10^38 claves; el programa explora 2^20 = 1 048 576 (una fracción de 2^-108 ≈ 3×10^-33). A ~3 M claves/s por núcleo (lo medido), recorrer 2^128 tomaría ≈ 10^24 años, de modo que la fuerza bruta real es inviable; el ejercicio solo es posible porque se restringe el espacio.
5. **Otras limitaciones:** nada es configurable (rango, mensaje, clave fijos en `#define`); `EVP_CipherInit_ex` + `set_padding` + `Final` se repiten por cada candidata (se hace el descifrado completo aunque el primer bloque ya diga que no); sin validación de entradas.

## 7. Mejoras propuestas e implementadas

Archivo: `busqueda_clave_aes_secuencial_mejorado.c`
Compilar: `gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_secuencial_mejorado.c -o busqueda_clave_aes_secuencial_mejorado -lcrypto`
Uso: `./busqueda_clave_aes_secuencial_mejorado [-n bits] [-k secreta] [-m mensaje]`

| # | Mejora | Tipo | Justificación | Propuso |
|---|---|---|---|---|
| M1 | Rango (`-n`), clave secreta (`-k`) y mensaje (`-m`) por línea de comandos | Flexibilidad | Permite probar distintos tamaños de espacio y posiciones de la clave (inicio/medio/final) sin recompilar; necesario para medir Speedup de forma justa. | Javier Benitez |
| M2 | Clave secreta aleatoria dentro del rango con `RAND_bytes` (si no se pasa `-k`) | Seguridad | Una secreta constante al inicio hace trivial la búsqueda y sesga las mediciones; con CSPRNG la posición es impredecible. | Javier Benitez |
| M3 | AES-128-**CBC** con IV aleatorio y relleno PKCS#7 en lugar de ECB | Seguridad / flexibilidad | Elimina el patrón de ECB (mismo texto → mismo cifrado) y admite mensajes de cualquier longitud (hasta 1024 B). | Javier Benitez |
| M4 | Clave de 128 bits reales: 8 bytes superiores aleatorios (prefijo conocido) + 8 inferiores = candidata | Seguridad | Evita la clave con ceros predecibles; el prefijo se trata como parte conocida para que el ataque siga siendo factible, y queda explícito que el rango explorado es solo un subconjunto de 2^128. | Javier Benitez |
| M5 | Prueba rápida: se descifra solo el primer bloque y se compara con el texto conocido; el descifrado completo + validación del relleno solo si coincide | Rendimiento | Con mensajes largos evita descifrar todos los bloques para cada una de las candidatas erróneas (coste por candidata constante, independiente del largo del mensaje). | Javier Benitez |
| M6 | Validación de argumentos (rango 1–40 bits, secreta < 2^n, mensaje 1–1024 B) | Seguridad / robustez | Evita desbordes de búfer y corridas sin sentido. | Javier Benitez |
| M7 | Inicializar el algoritmo/modo una sola vez y, por candidata, cambiar solo la clave (`EVP_DecryptInit_ex` con `cipher = NULL`) | Rendimiento | Evita reconfigurar el algoritmo en cada una de las 2^20 candidatas; solo se recalcula la expansión de clave. | Javier López |
| P3 | _(pegar aquí la mejora del integrante 3)_ | | | _(nombre)_ |

> Para el Speedup, mantener la clave fija en el mismo lugar entre corridas: `-k 12345` (cerca del inicio) o `-k 1048575` (peor caso, última candidata).

### Verificación realizada

Compilación: `capturas/05_compilar_mejorado.png`. Compila sin advertencias con `-Wall -Wextra`.

Ejecución: `capturas/06_ejecutar_mejorado.png`.

| Comando | Resultado |
|---|---|
| `-k 12345` | Clave 12345, `Puedes lograrlo!`, 0.006097 s |
| `-k 1048575 -m "mensaje largo de mas de dieciseis bytes"` | Clave 1048575, mensaje correcto, 0.383843 s (peor caso: recorre las 2^20 candidatas) |
| (sin argumentos, secreta aleatoria) | Clave encontrada 460937, `Puedes lograrlo!`, 0.169559 s |

Validación de argumentos (`capturas/07_validacion.png`):

| Comando | Resultado |
|---|---|
| `-k 99999999` | Rechazado: `La clave secreta debe ser < 2^20.` |

El caso `-k 12345` tarda prácticamente lo mismo que el original (~0.005–0.006 s) porque la clave sigue cerca del inicio del rango; el caso `-k 1048575` (última candidata, peor caso real) sí refleja el costo de recorrer el espacio completo de 2^20 claves, y es el que debe usarse como referencia para comparar con la versión paralela (ejercicio 8).
