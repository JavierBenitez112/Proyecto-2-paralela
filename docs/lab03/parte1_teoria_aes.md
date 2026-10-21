# Parte 1 – Investigación sobre AES (ejercicios 1–4)

**Integrante:** Javier López

---

## 1. ¿Qué es AES?

**AES (Advanced Encryption Standard)** es un algoritmo de **cifrado simétrico por bloques** estandarizado por el NIST en 2001 (FIPS PUB 197). Es una versión del algoritmo **Rijndael**, diseñado por los criptógrafos belgas Joan Daemen y Vincent Rijmen, que ganó el concurso público que el NIST organizó para reemplazar a DES, cuya clave de 56 bits ya podía romperse por fuerza bruta.

Características principales:

- **Simétrico:** se usa la **misma clave** para cifrar y descifrar.
- **Por bloques:** procesa siempre bloques de **128 bits (16 bytes)**.
- **Tres tamaños de clave**, que determinan el número de rondas:

  | Variante | Clave | Rondas | Subclaves de ronda |
  |---|---|---|---|
  | AES-128 | 128 bits (16 B) | 10 | 11 |
  | AES-192 | 192 bits (24 B) | 12 | 13 |
  | AES-256 | 256 bits (32 B) | 14 | 15 |

- **Red de sustitución–permutación (SPN):** cada ronda combina sustituciones no lineales (confusión) y permutaciones/mezclas lineales (difusión), en lugar de la estructura Feistel de DES.
- Trabaja sobre una matriz de estado de **4 × 4 bytes** y usa aritmética en el campo finito **GF(2⁸)** con el polinomio irreducible m(x) = x⁸ + x⁴ + x³ + x + 1.
- Es rápido en software y hardware; los procesadores modernos incluyen instrucciones dedicadas (**AES-NI** en x86, extensiones criptográficas en ARM).
- No se conoce ningún ataque práctico contra AES completo: la mejor opción contra una clave bien elegida sigue siendo la fuerza bruta, que para AES-128 implica hasta **2¹²⁸ ≈ 3.4 × 10³⁸** claves.

---

## 2. Campos de aplicación

1. **Comunicaciones en Internet (HTTPS/TLS):** cuando un navegador se conecta a un sitio seguro, después del intercambio de claves los datos se cifran normalmente con **AES-GCM** (TLS 1.2 y 1.3). Lo mismo aplica a correo (SMTPS/IMAPS) y APIs.
2. **Redes inalámbricas (Wi-Fi):** WPA2 usa **CCMP**, basado en AES-CCM; WPA3 usa AES-CCMP/GCMP para cifrar las tramas entre el dispositivo y el punto de acceso.
3. **Cifrado de discos y almacenamiento:** BitLocker (Windows), FileVault (macOS), LUKS/dm-crypt (Linux) y el almacenamiento de Android/iOS cifran discos completos con AES, normalmente en modo **XTS**.
4. **VPN y redes corporativas:** IPsec y OpenVPN protegen el tráfico del túnel con AES (CBC o GCM).
5. **Otras:** archivos comprimidos protegidos con contraseña (7-Zip y ZIP con AES-256), mensajería, bases de datos con cifrado en reposo, tarjetas inteligentes y pagos electrónicos.

---

## 3. Cifrado y descifrado con AES-128

### 3.1 Tamaño de la clave y procesamiento en bloques

- **Clave:** 128 bits = **16 bytes**. El espacio de claves tiene **2¹²⁸** posibilidades.
- **Bloque:** AES siempre cifra **16 bytes a la vez**, sin importar el tamaño de la clave. El bloque se carga en la **matriz de estado** de 4 × 4 bytes **por columnas**:

  ```
  entrada:  b0 b1 b2 ... b15

  estado:   | b0  b4  b8  b12 |
            | b1  b5  b9  b13 |
            | b2  b6  b10 b14 |
            | b3  b7  b11 b15 |
  ```

- **Mensajes de más de 16 bytes:** se dividen en bloques de 16 bytes y se usa un **modo de operación**:
  - **ECB:** cada bloque se cifra por separado con la misma clave. Bloques iguales producen cifrados iguales, por lo que se filtran patrones del mensaje (el ejemplo clásico es la imagen del pingüino). No se recomienda.
  - **CBC:** cada bloque se combina con XOR con el cifrado anterior; necesita un **IV** aleatorio.
  - **CTR / GCM:** convierten AES en un cifrado de flujo; GCM además **autentica** el mensaje. Es el modo recomendado hoy.
- **Relleno (padding):** si el último bloque no completa 16 bytes en ECB/CBC, se rellena (p. ej. PKCS#7). Si el mensaje mide exactamente 16 bytes, se puede desactivar el relleno (es lo que hace el programa del laboratorio).

### 3.2 Expansión de clave (Key Expansion)

AES-128 no usa la clave directamente en todas las rondas. A partir de los 16 bytes de la clave genera **11 subclaves de 16 bytes** (44 palabras de 32 bits, 176 bytes en total): una para la ronda inicial y una para cada una de las 10 rondas.

- Las primeras 4 palabras `w[0..3]` son la clave original.
- Para `i = 4 … 43`:
  - si `i` es múltiplo de 4: `w[i] = w[i-4] ⊕ SubWord(RotWord(w[i-1])) ⊕ Rcon[i/4]`
  - si no: `w[i] = w[i-4] ⊕ w[i-1]`
- `RotWord` rota los 4 bytes de la palabra una posición, `SubWord` aplica la S-box a cada byte y `Rcon` es una constante de ronda (01, 02, 04, 08, 10, 20, 40, 80, 1B, 36).

Gracias a esto, **cambiar un solo bit de la clave cambia todas las subclaves**. Además, cada clave candidata exige volver a calcular toda la expansión; en una búsqueda por fuerza bruta este costo se paga en cada intento.

### 3.3 Transformaciones principales

| Transformación | Qué hace | Propósito |
|---|---|---|
| **SubBytes** | Sustituye cada byte del estado usando la **S-box**: calcula el inverso multiplicativo en GF(2⁸) y luego aplica una transformación afín. Ej.: S(0x00) = 0x63, S(0x53) = 0xED. | **Confusión** (única parte no lineal del algoritmo). |
| **ShiftRows** | Desplaza cíclicamente a la izquierda la fila *r* del estado *r* posiciones (fila 0 queda igual, fila 1 se mueve 1, fila 2 se mueve 2, fila 3 se mueve 3). | **Difusión** entre columnas. |
| **MixColumns** | Multiplica cada columna, en GF(2⁸), por la matriz fija `[02 03 01 01; 01 02 03 01; 01 01 02 03; 03 01 01 02]`. | **Difusión** dentro de cada columna: cada byte de salida depende de los 4 bytes de entrada. |
| **AddRoundKey** | XOR del estado con la subclave de la ronda. | **Es el único paso que usa la clave.** |

Inversas para descifrar:

- **InvSubBytes:** usa la S-box inversa.
- **InvShiftRows:** desplaza las filas hacia la **derecha**.
- **InvMixColumns:** multiplica por la matriz inversa `[0E 0B 0D 09; 09 0E 0B 0D; 0D 09 0E 0B; 0B 0D 09 0E]`.
- **AddRoundKey** es su propia inversa porque `(x ⊕ k) ⊕ k = x`.

### 3.4 Pasos para cifrar (AES-128)

1. Dividir el texto en bloques de 16 bytes (con relleno o modo de operación si hace falta) y cargar cada bloque en el estado.
2. Expandir la clave de 16 bytes en las subclaves K₀ … K₁₀.
3. **Ronda inicial:** `AddRoundKey(K₀)`.
4. **Rondas 1 a 9:** `SubBytes → ShiftRows → MixColumns → AddRoundKey(Kᵢ)`.
5. **Ronda 10 (final):** `SubBytes → ShiftRows → AddRoundKey(K₁₀)`, **sin MixColumns**.
6. El estado resultante es el bloque cifrado de 16 bytes.

### 3.5 Pasos para descifrar (AES-128)

Se usan las mismas subclaves, **en orden inverso**:

1. Cargar el bloque cifrado en el estado y expandir la **misma clave**.
2. **Ronda inicial:** `AddRoundKey(K₁₀)`.
3. **Rondas 9 a 1:** `InvShiftRows → InvSubBytes → AddRoundKey(Kᵢ) → InvMixColumns`.
4. **Ronda final:** `InvShiftRows → InvSubBytes → AddRoundKey(K₀)`.
5. El estado resultante es el texto original (luego se quita el relleno si lo hubo).

### 3.6 Vector de prueba oficial (FIPS-197, Apéndice C.1)

```
Clave:         000102030405060708090a0b0c0d0e0f
Texto plano:   00112233445566778899aabbccddeeff
Texto cifrado: 69c4e0d86a7b0430d8cdb78070b4c55a
```

Con este vector se puede comprobar que una implementación de AES-128 es correcta.

---

## 4. Diagrama de flujo: cifrado y descifrado AES-128

> Para el PDF: copiar el bloque en https://mermaid.live y exportarlo como PNG o SVG.

```mermaid
flowchart TB
    K([Clave de 128 bits<br/>16 bytes]) --> KE[Expansión de clave<br/>RotWord · SubWord · Rcon]
    KE --> RK[[Subclaves K0 ... K10<br/>11 × 16 bytes]]

    subgraph CIFRADO
        direction TB
        P([Texto plano<br/>bloque de 16 bytes]) --> S0[Cargar en estado 4×4]
        S0 --> A0[AddRoundKey K0]
        A0 --> R1{"i = 1"}
        R1 --> SB[SubBytes]
        SB --> SR[ShiftRows]
        SR --> MC[MixColumns]
        MC --> AK[AddRoundKey Ki]
        AK --> C1{"¿i < 9?"}
        C1 -- "sí: i = i + 1" --> SB
        C1 -- no --> FSB[SubBytes]
        FSB --> FSR[ShiftRows]
        FSR --> FAK[AddRoundKey K10]
        FAK --> C([Texto cifrado<br/>16 bytes])
    end

    subgraph DESCIFRADO
        direction TB
        C2([Texto cifrado<br/>16 bytes]) --> D0[AddRoundKey K10]
        D0 --> R2{"i = 9"}
        R2 --> ISR[InvShiftRows]
        ISR --> ISB[InvSubBytes]
        ISB --> IAK[AddRoundKey Ki]
        IAK --> IMC[InvMixColumns]
        IMC --> C3{"¿i > 1?"}
        C3 -- "sí: i = i − 1" --> ISR
        C3 -- no --> FISR[InvShiftRows]
        FISR --> FISB[InvSubBytes]
        FISB --> FIAK[AddRoundKey K0]
        FIAK --> P2([Texto plano<br/>16 bytes])
    end

    C -. se transmite .-> C2

    RK -. K0 .-> A0
    RK -. K1..K9 .-> AK
    RK -. K10 .-> FAK
    RK -. K10 .-> D0
    RK -. K9..K1 .-> IAK
    RK -. K0 .-> FIAK
```

**Cómo interviene la clave:**

- La clave entra **una sola vez**, en la expansión de clave, y produce 11 subclaves.
- Las subclaves solo se usan en **AddRoundKey**, una vez por ronda (11 veces en total en cada sentido).
- El cifrado las usa en orden **K0 → K10**; el descifrado usa **las mismas** en orden **K10 → K0**. Por eso AES es simétrico: con una clave distinta las subclaves cambian por completo y el descifrado devuelve bytes sin sentido. El programa del laboratorio se basa en esto para saber si una clave candidata es correcta.

---

## Mejora propuesta para el ejercicio 7

**Propuesta por:** Javier López – **Tipo:** rendimiento

**Mejora:** inicializar el algoritmo AES **una sola vez** fuera del ciclo y, para cada candidata, cambiar solo la clave.

En el programa original, `crypt_block()` llama a `EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL, encrypt)` en cada iteración. Así, en cada una de las 2²⁰ candidatas, OpenSSL vuelve a seleccionar y configurar el algoritmo y el modo, aunque estos nunca cambian. Lo único que debe cambiar en cada intento es la **clave**, porque es lo que obliga a recalcular la expansión de clave (sección 3.2).

**Justificación:**

- Según la teoría, por cada candidata solo hace falta recalcular las subclaves y descifrar un bloque. Volver a elegir el algoritmo es trabajo repetido que no aporta nada.
- OpenSSL permite pasar `cipher = NULL` en `EVP_CipherInit_ex` para **reutilizar el algoritmo ya configurado** en el contexto y cambiar solo la clave.
- La versión MPI también se beneficia, porque cada proceso usa su propio contexto con la misma lógica.

**Cambio sugerido (para que P2 lo integre):**

```c
/* Una sola vez, antes del ciclo: selecciona AES-128-ECB en modo descifrado. */
if (EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), NULL, NULL, NULL, 0) != 1) {
    fail("Error al inicializar AES.");
}

/* Dentro del ciclo: solo se cambia la clave (cipher = NULL reutiliza AES-128-ECB). */
make_key(candidate, key);
if (EVP_CipherInit_ex(ctx, NULL, NULL, key, NULL, 0) != 1) {
    fail("Error al cambiar la clave.");
}
if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) {
    fail("Error al configurar el relleno.");
}
/* ... EVP_CipherUpdate / EVP_CipherFinal_ex igual que antes ... */
```

**Cómo evidenciarla:** medir el tiempo del secuencial antes y después del cambio, con la misma clave secreta y el mismo rango (promedio de 5 ejecuciones), y verificar que se recupera la misma clave y el mismo mensaje.

---

## Referencias

- NIST. *FIPS PUB 197: Advanced Encryption Standard (AES)*, 2001 (actualizado en 2023).
- Daemen, J. y Rijmen, V. *The Design of Rijndael*. Springer, 2002.
- NIST. *SP 800-38A: Recommendation for Block Cipher Modes of Operation*, 2001.
- Documentación de OpenSSL: `EVP_EncryptInit(3)` / `EVP_CipherInit_ex`.
