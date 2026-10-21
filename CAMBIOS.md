# Cambios para cerrar el Proyecto 02

Resumen de lo que se corrigió o agregó respecto a la versión anterior del repositorio.

## Código

| Cambio | Archivo | Motivo |
|---|---|---|
| Aviso FOUND con `MPI_Isend` + conteo exacto de avisos pendientes + `MPI_Waitall` | `src/proyecto02/busqueda_mpi.c` | Con `MPI_Send` la terminación dependía del buffer interno de MPI (con envío síncrono el programa se bloqueaba) y un solo `MPI_Iprobe` no garantizaba vaciar los avisos antes de `MPI_Finalize`. |
| `main` dividido en `difundir_mensaje`, `buscar_en_tramo`, `avisar_encontrada`, `hay_aviso_found`, `recibir_avisos_pendientes`, `imprimir_*` | `busqueda_mpi.c` | Orden y claridad (rúbrica); el catálogo del Anexo 1 ahora coincide con el código. |
| Se reporta el proceso que realmente halló la clave, sus intentos y su tiempo | `busqueda_mpi.c` | Antes se calculaba con una fórmula. |
| Metadatos del `MPI_Bcast` como `uint64_t` | `busqueda_mpi.c` | `MPI_LONG` es de 32 bits en Windows y fallaba con claves ≥ 2^63. |
| Motor AES-NI con detección en tiempo de ejecución y opción `-a auto\|evp\|aesni` | `src/common/aes_texto.c`, `include/aes_texto.h` | La prueba de candidatas es ~2.7× más rápida; EVP queda como respaldo y para comparar. |
| Validación del relleno PKCS#7 cuando aparece la frase | `aes_texto.c` | Descarta falsos positivos (frases cortas) sin costo en las candidatas incorrectas. |
| `aes_texto -d` acepta archivos cifrados de hasta 1 MiB de texto | `src/proyecto02/aes_texto.c` | El `.bin` mide texto + IV + relleno y superaba el límite de lectura. |
| Validación de opciones: letra desconocida, `-t inf`, aviso de clave fuera del rango | `aes_texto.c` | Programación defensiva. |
| Makefile: rutinas del Lab 03 y del P02 separadas; solo `busqueda_mpi` usa `mpicc`; objetivos `test`, `demo`, `bitacora`, `informe` | `Makefile` | Los programas del P02 enlazaban código del Lab 03 que no usaban. |

## Pruebas y evidencia

- `make test` (`scripts/pruebas.sh`): 169 pruebas con ambas estrategias, ambos motores, P = 1..5, claves en límites de bloque, clave fuera del rango, archivo cifrado como entrada y argumentos inválidos.
- `make bitacora` (`scripts/bitacora.sh`): todas las mediciones del Anexo 2 en `resultados/` (salida exacta de cada comando, `tiempos.csv`, `resumen.md` y capturas PNG).
- `make informe` (`scripts/generar_informe.py`): regenera `docs/proyecto02/Informe_Proyecto02_Paralela.docx` y `.pdf`, y la estrategia alternativa, a partir de los borradores en `docs/proyecto02/borrador/` y de los datos de `resultados/`.

## Informe

- Carátula: CC3069 y "Proyecto 02". Número de página en el pie; índice con páginas reales.
- Todos los `[PENDIENTE]` cerrados con datos medidos (Tablas 3–7, 11 y 12; Figuras 4–19).
- Corregido: la prueba de candidata (se descifran 3 bloques y se busca la frase en cualquier posición), la terminación con MPI (sección 3.4 y Figura 3 nueva), el catálogo de funciones y librerías, la tasa de referencia (la del Lab 03 ya no aplica) y las citas (IEEE numerado, una sola lista de referencias y la cita textual con su traducción a pie de página).

## Pendiente para el equipo

- Las mediciones de `resultados/` se hicieron en un equipo de **2 núcleos**, así que con `-np 4` el Speedup máximo es ≈ 2. Para la entrega conviene correr `make bitacora && make informe` en una máquina con 4 o más núcleos (WSL sirve); el informe se regenera solo con esos datos.
- Confirmar que la estrategia base (intercalada) es la del MPI del Laboratorio 03; el informe la presenta como la base implementada en este proyecto.
