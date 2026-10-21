/*----------------------------------------------------------------------
 * CC3069 - Computacion Paralela y Distribuida - Proyecto 02 (incisos 2-4)
 * busqueda_mpi.c - Fuerza bruta AES-128 paralela con Open MPI
 *
 * Reparte el rango [0, 2^bits) entre los procesos de MPI_COMM_WORLD con
 * una de dos estrategias y busca la clave cuyo descifrado contiene la
 * frase de busqueda. El primero que la encuentra avisa a los demas; si
 * nadie la encuentra, cada proceso agota su tramo y el rango se declara
 * sin exito.
 *
 *   Estrategia 1 (base):        k = rank, rank+size, rank+2*size, ...
 *   Estrategia 2 (alternativa): un bloque contiguo de ~N/p claves por
 *                               proceso (los primeros N mod p llevan una
 *                               clave mas; ver calcular_tramo)
 *
 * Uso (todos los procesos reciben los mismos argumentos):
 *   mpirun -np 4 busqueda_mpi -i texto.txt -k CLAVE [-e 1|2] [-f frase]
 *                             [-n bits] [-t seg] [-a auto|evp|aesni]
 *   mpirun -np 4 busqueda_mpi -c cifrado.bin [-e 1|2] [-f frase] [-n bits]
 *                             [-t seg] [-a auto|evp|aesni]
 *
 * Comunicacion entre procesos:
 *   1. MPI_Bcast:      el proceso 0 cifra (o lee) el mensaje y lo difunde,
 *                      asi todos buscan exactamente el mismo cifrado.
 *   2. MPI_Isend:      quien encuentra la clave avisa a cada proceso con un
 *                      mensaje de etiqueta TAG_FOUND (no bloqueante).
 *   3. MPI_Iprobe:     cada INTERVALO_REVISION candidatas cada proceso
 *                      revisa, sin bloquearse, si llego un aviso.
 *   4. MPI_Allreduce / MPI_Reduce: se reune la clave, el tiempo maximo y
 *                      las estadisticas; se reciben los avisos que hayan
 *                      quedado pendientes para cerrar MPI limpiamente.
 *----------------------------------------------------------------------*/
#include "aes_texto.h"

#include <mpi.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define TAG_FOUND    1            /* un proceso avisa que encontro la clave */
#define SIN_CLAVE    UINT64_MAX   /* marca de "no encontrada"               */

/* Lo que cada proceso sabe al terminar su busqueda local. */
typedef struct {
    uint64_t clave;            /* clave hallada por este proceso o SIN_CLAVE */
    uint64_t probadas;         /* candidatas probadas por este proceso       */
    int avisos_recibidos;      /* avisos FOUND leidos durante la busqueda    */
    int por_limite;            /* 1 si se detuvo por el limite de tiempo -t  */
    double t_fin;              /* MPI_Wtime() al salir del ciclo             */
} resultado_local_t;

static void uso(const char *prog)
{
    fprintf(stderr,
        "Uso: mpirun -np P %s (-i texto.txt -k CLAVE | -c cifrado.bin)\n"
        "        [-e 1|2] [-f frase] [-n bits] [-t seg] [-a auto|evp|aesni]\n"
        "     estrategia 1 = intercalada (base), 2 = bloques (alternativa)\n",
        prog);
}

/* ================================================================ preparacion */

/* El proceso 0 prepara el mensaje (cifrando -i con -k o leyendo -c) y lo
 * difunde a todos (IV + cifrado). Los metadatos viajan como uint64_t para
 * no depender del tamano de long (32 bits en Windows). 0 / -1, igual en
 * todos los procesos. */
static int difundir_mensaje(const opciones_t *op, int rank, mensaje_t *m)
{
    /* {preparado (0/1), longitud del cifrado, hay referencia, clave ref.} */
    uint64_t meta[4] = {0, 0, 0, 0};
    memset(m, 0, sizeof *m);
    if (rank == 0 && preparar_mensaje(op, m) == 0) {
        meta[0] = 1;
        meta[1] = m->cifrado.len;
        meta[2] = (uint64_t)m->tiene_referencia;
        meta[3] = m->clave_referencia;
    }
    MPI_Bcast(meta, 4, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    if (meta[0] == 0)
        return -1;                       /* el proceso 0 ya reporto el error */

    if (rank != 0) {
        m->cifrado.len = (size_t)meta[1];
        m->cifrado.datos = malloc(m->cifrado.len);
        if (m->cifrado.datos == NULL)
            reportar_error("memoria insuficiente para el mensaje cifrado");
        m->tiene_referencia = (int)meta[2];
        m->clave_referencia = meta[3];
    }
    /* Si algun proceso no pudo reservar memoria, todos salen juntos. */
    int ok = m->cifrado.datos != NULL, todos_ok = 0;
    MPI_Allreduce(&ok, &todos_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);
    if (!todos_ok) {
        mensaje_liberar(m);
        return -1;
    }
    MPI_Bcast(m->iv, AES_IV_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(m->cifrado.datos, (int)m->cifrado.len, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    return 0;
}

/* ================================================================ comunicacion */

/* Revisa sin bloquear si otro proceso aviso que encontro la clave; si hay
 * aviso lo recibe y devuelve 1 (clave en *k), si no 0. */
static int hay_aviso_found(uint64_t *k)
{
    int hay = 0;
    MPI_Status st;
    MPI_Iprobe(MPI_ANY_SOURCE, TAG_FOUND, MPI_COMM_WORLD, &hay, &st);
    if (!hay)
        return 0;
    MPI_Recv(k, 1, MPI_UINT64_T, st.MPI_SOURCE, TAG_FOUND, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    return 1;
}

/* Envia la clave *k a todos los demas procesos con MPI_Isend. Los envios
 * son no bloqueantes para que el proceso pueda entrar a las reducciones
 * aunque otro todavia no haya llegado a recibir (con MPI_Send bloqueante
 * eso dependeria del buffer interno de MPI). *k y reqs deben seguir vivos
 * hasta MPI_Waitall. Devuelve cuantas solicitudes quedaron en reqs. */
static int avisar_encontrada(const uint64_t *k, int rank, int size, MPI_Request *reqs)
{
    int n = 0;
    for (int d = 0; d < size; d++)
        if (d != rank)
            MPI_Isend(k, 1, MPI_UINT64_T, d, TAG_FOUND, MPI_COMM_WORLD, &reqs[n++]);
    return n;
}

/* Recibe los avisos que todavia no se leyeron. Cada proceso que encontro
 * la clave envio exactamente un aviso a cada uno de los demas, asi que el
 * numero esperado se conoce y no se depende de un MPI_Iprobe "a tiempo". */
static void recibir_avisos_pendientes(const resultado_local_t *r)
{
    int encontre = r->clave != SIN_CLAVE, encontradores = 0;
    MPI_Allreduce(&encontre, &encontradores, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    int pendientes = encontradores - encontre - r->avisos_recibidos;
    for (int i = 0; i < pendientes; i++) {
        uint64_t descartada;
        MPI_Recv(&descartada, 1, MPI_UINT64_T, MPI_ANY_SOURCE, TAG_FOUND,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
}

/* ================================================================ busqueda */

/* Recorre el tramo del proceso. Se detiene al encontrar la clave, al recibir
 * un aviso FOUND o al pasar el limite de tiempo (si limite > 0). La revision
 * de avisos y del reloj se hace cada INTERVALO_REVISION candidatas para que
 * su costo sea despreciable frente al descifrado. */
static void buscar_en_tramo(buscador_t *bus, const tramo_t *tramo, double limite,
                            double t_inicio, resultado_local_t *r)
{
    memset(r, 0, sizeof *r);
    r->clave = SIN_CLAVE;

    for (uint64_t i = 0; i < tramo->cantidad; i++) {
        uint64_t k = tramo->inicio + i * tramo->paso;
        int res = probar_clave(bus, k);
        r->probadas++;
        if (res < 0) {
            /* Un fallo de OpenSSL no se puede recuperar: se aborta todo el
             * trabajo para no dejar a los demas procesos buscando solos. */
            reportar_error("OpenSSL fallo durante la busqueda");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
        if (res > 0) {
            r->clave = k;
            break;
        }
        if ((i & (INTERVALO_REVISION - 1)) == INTERVALO_REVISION - 1) {
            uint64_t k_otro;
            if (hay_aviso_found(&k_otro)) {
                r->avisos_recibidos = 1;
                break;
            }
            if (limite > 0 && MPI_Wtime() - t_inicio >= limite) {
                r->por_limite = 1;
                break;
            }
        }
    }
    r->t_fin = MPI_Wtime();
}

/* ================================================================ reporte */

static void imprimir_encabezado(const opciones_t *op, enum estrategia e,
                                enum motor_aes motor, int size, uint64_t total)
{
    printf("=== Busqueda AES-128 por fuerza bruta (MPI, %d procesos) ===\n", size);
    printf("Estrategia:           %s\n", nombre_estrategia(e));
    printf("Rango:                [0, 2^%u) = %" PRIu64 " candidatas\n", op->bits, total);
    printf("Frase de busqueda:    \"%s\"\n", op->frase);
    printf("Motor AES:            %s\n", nombre_motor(motor));
    if (op->limite > 0)
        printf("Limite de tiempo:     %.1f s\n", op->limite);
    fflush(stdout);
}

/* Descifra con la clave hallada y muestra clave, proceso y texto. */
static void imprimir_clave(const mensaje_t *msg, uint64_t clave_dec, int duenio,
                           uint64_t intentos, double t_hallazgo)
{
    unsigned char clave[AES_KEY_LEN];
    clave_desde_u64(clave_dec, clave);
    unsigned char *texto = malloc(msg->cifrado.len);
    int plen = texto ? descifrar_texto(clave, msg->iv, msg->cifrado.datos,
                                       (int)msg->cifrado.len, texto) : -1;
    printf("Resultado:            CLAVE ENCONTRADA\n");
    printf("Clave (decimal):      %" PRIu64 "\n", clave_dec);
    printf("Clave AES-128:        ");
    imprimir_hex(clave, AES_KEY_LEN);
    printf("\n");
    printf("Hallada por proceso:  %d (tras %" PRIu64 " intentos en su tramo, %.6f s)\n",
           duenio, intentos, t_hallazgo);
    if (plen >= 0) {
        printf("Texto descifrado:     \"");
        imprimir_texto(texto, (size_t)plen);
        printf("\"\n");
    } else {
        printf("Texto descifrado:     (relleno invalido: posible falso positivo)\n");
    }
    free(texto);
}

/* ================================================================ main */

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Todos validan los argumentos (son los mismos), solo el 0 informa. */
    opciones_t op;
    char error[160];
    if (parse_opciones(argc, argv, 1, &op, error, sizeof error) != 0) {
        if (rank == 0) { fprintf(stderr, "Error: %s\n", error); uso(argv[0]); }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    mensaje_t msg;
    if (difundir_mensaje(&op, rank, &msg) != 0) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    enum estrategia estr = op.estrategia == 2 ? ESTR_BLOQUES : ESTR_INTERCALADA;
    const uint64_t total = UINT64_C(1) << op.bits;
    tramo_t tramo = calcular_tramo(estr, total, size, rank);

    buscador_t bus;
    if (buscador_iniciar(&bus, msg.iv, msg.cifrado.datos, (int)msg.cifrado.len,
                         (const unsigned char *)op.frase, strlen(op.frase),
                         op.motor) != 0) {
        mensaje_liberar(&msg);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
    if (rank == 0)
        imprimir_encabezado(&op, estr, bus.motor, size, total);

    /* ------------------------------------------------ busqueda (cronometrada) */
    MPI_Barrier(MPI_COMM_WORLD);               /* todos arrancan juntos */
    double t_inicio = MPI_Wtime();

    resultado_local_t res;
    buscar_en_tramo(&bus, &tramo, op.limite, t_inicio, &res);

    MPI_Request *reqs = NULL;
    int nreqs = 0;
    if (res.clave != SIN_CLAVE) {
        reqs = malloc((size_t)(size > 1 ? size - 1 : 1) * sizeof *reqs);
        if (reqs == NULL) {
            reportar_error("memoria insuficiente para los avisos");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
        nreqs = avisar_encontrada(&res.clave, rank, size, reqs);
    }

    /* ------------------------------------------------ reduccion de resultados */
    /* Clave global: la menor de las halladas (SIN_CLAVE = ninguna). */
    uint64_t clave_global = SIN_CLAVE;
    MPI_Allreduce(&res.clave, &clave_global, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);

    /* Quien la hallo, con cuantos intentos y en que momento. */
    int soy_duenio = clave_global != SIN_CLAVE && res.clave == clave_global;
    int duenio = INT_MAX, duenio_global = INT_MAX;
    if (soy_duenio) duenio = rank;
    MPI_Allreduce(&duenio, &duenio_global, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    uint64_t intentos = rank == duenio_global ? res.probadas : 0, intentos_duenio = 0;
    double t_hallazgo = rank == duenio_global ? res.t_fin - t_inicio : 0.0, t_hallazgo_max = 0.0;
    MPI_Reduce(&intentos, &intentos_duenio, 1, MPI_UINT64_T, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_hallazgo, &t_hallazgo_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    /* Tiempo de la busqueda = el del ultimo proceso en detenerse. */
    double t_local = res.t_fin - t_inicio, tiempo = 0.0;
    MPI_Reduce(&t_local, &tiempo, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    uint64_t probadas_total = 0;
    MPI_Reduce(&res.probadas, &probadas_total, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    int por_limite_global = 0;
    MPI_Reduce(&res.por_limite, &por_limite_global, 1, MPI_INT, MPI_LOR, 0, MPI_COMM_WORLD);

    /* Cerrar la comunicacion punto a punto antes de MPI_Finalize. */
    recibir_avisos_pendientes(&res);
    if (nreqs > 0)
        MPI_Waitall(nreqs, reqs, MPI_STATUSES_IGNORE);
    free(reqs);

    /* ------------------------------------------------ reporte (solo rank 0) */
    if (rank == 0) {
        double tasa = tiempo > 0 ? (double)probadas_total / tiempo : 0.0;
        if (clave_global != SIN_CLAVE)
            imprimir_clave(&msg, clave_global, duenio_global, intentos_duenio, t_hallazgo_max);
        else if (por_limite_global)
            printf("Resultado:            NO ENCONTRADA (limite de %.1f s alcanzado)\n", op.limite);
        else
            printf("Resultado:            NO ENCONTRADA en todo el rango\n");

        printf("Claves probadas:      %" PRIu64 " (suma de los %d procesos)\n",
               probadas_total, size);
        printf("Tiempo de busqueda:   ");
        imprimir_duracion(tiempo);
        printf("\nTasa agregada:        %.0f claves/s (%.0f por proceso)\n",
               tasa, tasa / size);

        /* Busqueda cortada por -t: estimar cuanto tardaria la clave de referencia. */
        if (clave_global == SIN_CLAVE && por_limite_global && msg.tiene_referencia &&
            msg.clave_referencia < total && tasa > 0) {
            int r_ref;
            uint64_t n_ref;
            ubicar_clave(estr, total, size, msg.clave_referencia, &r_ref, &n_ref);
            printf("Estimacion para la clave %" PRIu64 " (estrategia %d):\n",
                   msg.clave_referencia, op.estrategia);
            printf("  proceso %d, %" PRIu64 " intentos -> ", r_ref, n_ref);
            imprimir_duracion((double)n_ref / (tasa / size));
            printf("\n");
        }
        fflush(stdout);
    }

    buscador_liberar(&bus);
    mensaje_liberar(&msg);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
