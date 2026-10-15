# Laboratorio 03 – Reparto de trabajo (3 integrantes)

**Curso:** CC3069 – Computación Paralela y Distribuida
**Objetivo:** analizar y mejorar el programa secuencial de búsqueda de claves AES, hacer su versión paralela con Open MPI y medir tiempos y Speedup.

| Integrante | Rol | Ejercicios | Entregable principal |
|---|---|---|---|
| **P1** – Javier López | Teoría AES | 1, 2, 3, 4 | `parte1_teoria_aes.md` (texto + diagrama de flujo) |
| **P2** – Javier Benitez | Programa secuencial | 5, 6, 7 | `busqueda_clave_aes_secuencial_mejorado.c` + análisis |
| **P3** – _(nombre)_ | Paralelización MPI | 8 | `busqueda_clave_aes_mpi.c` + tabla de Speedup |

> **Regla del enunciado:** cada mejora del ejercicio 7 lleva el **nombre de quien la propuso**. Cada integrante propone **al menos una** mejora (P1 ya tiene la suya en su archivo). P2 se encarga de integrarlas todas en el código.

---

## P1 – Teoría de AES (ejercicios 1–4)

- [x] **Ej. 1:** qué es AES (origen, características, tamaños de clave y número de rondas).
- [x] **Ej. 2:** al menos 3 campos de aplicación.
- [x] **Ej. 3:** pasos para cifrar y descifrar con AES-128:
  - tamaño de la clave (128 bits) y procesamiento en bloques de 128 bits;
  - transformaciones: `SubBytes`, `ShiftRows`, `MixColumns`, `AddRoundKey` (y sus inversas), expansión de clave.
- [x] **Ej. 4:** diagrama de flujo de cifrado y descifrado que muestra cómo interviene la clave.
- [x] Proponer 1 mejora para el ejercicio 7 (con justificación) → se la pasa a P2.
- [ ] Exportar el diagrama Mermaid a imagen (https://mermaid.live → PNG) para pegarlo en el PDF.
- [ ] **Armar el PDF final** juntando las secciones de los tres.

## P2 – Programa secuencial (ejercicios 5–7)

- [x] **Ej. 5:** analizar si `busqueda_clave_aes_secuencial.c` usa bien AES-128. Contrastar con la teoría de P1:
  - clave de 16 bytes (`make_key`) → ¿es una clave de 128 bits *real* o solo 64 bits útiles con 8 bytes en cero?
  - bloque de 16 bytes (`MESSAGE_LEN`), sin relleno;
  - `EVP_aes_128_ecb()` para cifrar y descifrar.
- [x] **Ej. 6:** instalar y ejecutar en Linux/WSL, **con capturas** que muestren comandos y resultado:
  ```bash
  sudo apt update
  sudo apt install libssl-dev
  gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_secuencial.c -o busqueda_clave_aes_secuencial -lcrypto
  ./busqueda_clave_aes_secuencial
  ```
  Ejecutar varias veces (p. ej. 5) y reportar el promedio del tiempo.
- [x] **Ej. 6 (errores y limitaciones):** comentar al menos:
  - construcción de la clave (determinista, ceros a la izquierda, solo 2^20 candidatas);
  - modo ECB (bloques iguales → cifrados iguales; aquí es 1 bloque, pero no escala a mensajes largos);
  - verificar con el texto original completo (en un ataque real no se conoce todo el texto plano);
  - espacio total de AES-128 (2^128 ≈ 3.4×10^38) vs. rango explorado (2^20 = 1 048 576).
- [x] **Ej. 7:** proponer **≥ 1 mejora propia** e **implementar todas** (las de P1, P2 y P3) en `busqueda_clave_aes_secuencial_mejorado.c`. Ideas:
  - rango, clave secreta y mensaje por argumentos de línea de comandos (flexibilidad);
  - generar la clave secreta con `RAND_bytes` dentro del rango en vez de dejarla fija (seguridad del ejercicio);
  - usar un modo con IV o autenticado (CBC/CTR/GCM) si el mensaje pasa de 16 bytes.
- [ ] **Entregar el secuencial mejorado a P3 lo antes posible** (P3 depende de él).

## P3 – Versión paralela con Open MPI (ejercicio 8)

- [ ] Instalar Open MPI: `sudo apt install openmpi-bin libopenmpi-dev`.
- [ ] Escribir `busqueda_clave_aes_mpi.c` a partir del secuencial **mejorado** de P2:
  - repartir `[0, TOTAL_KEYS)` entre `size` procesos **sin omitir ni repetir** candidatas (bloques contiguos con el residuo repartido, o intercalado `key = rank; key += size`);
  - coordinar la finalización: cuando un proceso encuentra la clave, avisar a los demás (p. ej. `MPI_Iprobe`/`MPI_Irecv` revisado cada N claves, o `MPI_Allreduce` periódico) y que todos terminen también si se agota el rango;
  - medir con `MPI_Wtime()` y que el rank 0 imprima clave, mensaje y tiempo.
- [ ] Compilar y ejecutar **con capturas**:
  ```bash
  mpicc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_mpi.c -o busqueda_clave_aes_mpi -lcrypto
  mpirun -np 2 ./busqueda_clave_aes_mpi
  mpirun -np 3 ./busqueda_clave_aes_mpi
  mpirun -np 4 ./busqueda_clave_aes_mpi
  ```
  (si hay pocos núcleos: `mpirun --oversubscribe -np 4 ...`)
- [ ] Verificar que secuencial y MPI recuperan **la misma clave y mensaje**.
- [ ] Llenar la tabla (promedio de varias corridas) con `Speedup = T_secuencial / T_paralelo(n)`:

  | Procesos (n) | Tiempo (s) | Speedup |
  |---|---|---|
  | 1 (secuencial) |  | 1.00 |
  | 2 |  |  |
  | 3 |  |  |
  | 4 |  |  |

- [ ] Proponer ≥ 1 mejora (p. ej. de rendimiento en la distribución) con su nombre.
- [ ] Comentar los resultados: ¿por qué el Speedup cambia según dónde quede la clave dentro del reparto? (Ojo: con `SECRET_KEY = 12345` y bloques contiguos, el rank 0 la encuentra casi al inicio.)

---

## Orden sugerido

1. P1 y P2 arrancan en paralelo; P3 instala Open MPI y prepara el esqueleto MPI con el código original.
2. P2 entrega el secuencial mejorado → P3 lo adapta a MPI.
3. Todos mandan su sección + capturas a P1, que arma el PDF.

## Entrega final

- PDF con respuestas y capturas.
- `busqueda_clave_aes_secuencial_mejorado.c`
- `busqueda_clave_aes_mpi.c`
