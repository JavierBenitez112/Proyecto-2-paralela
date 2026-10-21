#!/usr/bin/env python3
"""Genera el informe final del Proyecto 02 a partir del borrador y de los
resultados reales de la bitacora (scripts/bitacora.sh).

  entrada:  docs/proyecto02/borrador/Informe_Proyecto02_Paralela_borrador.docx
            resultados/tiempos.csv, resultados/salidas/, resultados/capturas/
            docs/proyecto02/figuras/fig3_flujo_mpi.png
  salida:   docs/proyecto02/Informe_Proyecto02_Paralela.docx y .pdf

Todas las cifras de las tablas y del analisis se calculan desde tiempos.csv,
asi que si se vuelve a correr la bitacora en otra maquina basta con
`make informe` para regenerar el documento con esos datos.

Uso:  python3 scripts/generar_informe.py
"""
import copy
import csv
import glob
import os
import re
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict

import docx
from docx.oxml.ns import qn
from docx.shared import Inches
from lxml import etree

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BORRADOR = os.path.join(RAIZ, "docs/proyecto02/borrador/Informe_Proyecto02_Paralela_borrador.docx")
SALIDA = os.path.join(RAIZ, "docs/proyecto02/Informe_Proyecto02_Paralela.docx")
RES = os.path.join(RAIZ, "resultados")
FIG3 = os.path.join(RAIZ, "docs/proyecto02/figuras/fig3_flujo_mpi.png")
FECHA = os.environ.get("FECHA_INFORME", "GUATEMALA, 21 de octubre de 2026")

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
ANIO = 365.25 * 86400
CLAVES = {"a": 123456, "b": 18014398509481984, "c": 18014398509481985}
NOMBRE_CLAVE = {"a": "a) 123456", "b": "b) 18014398509481984 (2^56/4)",
                "c": "c) 18014398509481985 (2^56/4 + 1)"}


# =================================================================== datos

def cargar_datos():
    filas = list(csv.DictReader(open(os.path.join(RES, "tiempos.csv"), encoding="utf-8")))
    g = defaultdict(list)
    for f in filas:
        g[(f["prueba"], f["programa"], f["estrategia"], int(f["procesos"]))].append(f)
    datos = {}
    for k, fs in g.items():
        est = any(f["estimacion"] for f in fs)
        ts = [float(f["estimacion"].split()[0]) if f["estimacion"] else float(f["tiempo_s"])
              for f in fs]
        medidos = [float(f["tiempo_s"]) for f in fs]
        tasas = [float(f["tasa_claves_s"]) for f in fs]
        datos[k] = {
            "t": statistics.mean(ts),
            "sd": statistics.stdev(ts) if len(ts) > 1 else 0.0,
            "medido": statistics.mean(medidos),
            "est": est,
            "n": len(fs),
            "tasa": statistics.mean(tasas),
            "tasa_proc": statistics.mean(tasas) / k[3],
            "resultado": fs[0]["resultado"],
            "filas": fs,
        }
    return datos


def leer(ruta):
    with open(ruta, encoding="utf-8", errors="replace") as f:
        return f.read()


def equipo():
    txt = leer(os.path.join(RES, "salidas/00_equipo.txt"))
    lin = [l for l in txt.splitlines() if not l.startswith("$")]
    cpu = next((l.split(":", 1)[1].strip() for l in lin if "Model name" in l), "CPU desconocida")
    nucleos = int(next((l for l in lin if l.strip().isdigit()), "0"))
    so = lin[0] if lin else ""
    mpi = next((l for l in lin if "Open MPI" in l), "Open MPI")
    ssl = next((l for l in lin if l.startswith("OpenSSL")), "OpenSSL")
    ssl = ssl.split(" (")[0]
    mpi = mpi.replace("mpirun (Open MPI)", "Open MPI").strip()
    ssl = " ".join(ssl.split()[:2])            # "OpenSSL 3.0.13"
    return {"cpu": cpu, "nucleos": nucleos, "so": so, "mpi": mpi, "ssl": ssl}


def captura(patron):
    r = sorted(glob.glob(os.path.join(RES, "capturas", patron)))
    if not r:
        sys.exit(f"falta la captura {patron}; corra scripts/bitacora.sh")
    return r[0]


def comando(patron):
    """Primera linea ($ ...) de la salida guardada que coincide con el patron."""
    r = sorted(glob.glob(os.path.join(RES, "salidas", patron)))
    return leer(r[0]).splitlines()[0][2:] if r else ""


# =================================================================== formato numerico

def miles(n):
    return f"{int(n):,}".replace(",", "\u00a0")   # espacio no separable


def pm(media, sd):
    """Promedio ± desviacion en la misma unidad: 1.25 ± 0.74 ms."""
    for lim, esc, u in ((1, 1, "s"), (1e-3, 1e3, "ms"), (0, 1e6, "µs")):
        if media >= lim:
            return f"{media * esc:.2f} ± {sd * esc:.2f} {u}"


def dec(x, nd=2):
    return f"{x:.{nd}f}"


def t_txt(s, est=False):
    """Duracion legible con coma decimal no; se usa punto como en el borrador."""
    pref = "≈ " if est else ""
    if s >= ANIO:
        return f"{pref}{s / ANIO:.1f} años"
    if s >= 86400:
        return f"{pref}{s / 86400:.1f} días"
    if s >= 1:
        return f"{pref}{s:.3f} s"
    if s >= 1e-3:
        return f"{pref}{s * 1000:.2f} ms"
    return f"{pref}{s * 1e6:.2f} µs"


def sci(x):
    if x >= 1e4:
        m, e = f"{x:.2e}".split("e")
        return f"{m}×10^{int(e)}"
    return f"{x:.2f}"


# =================================================================== XML

def W_(tag):
    return qn("w:" + tag)


class Doc:
    """Ayudas para editar el cuerpo del borrador conservando su formato."""

    def __init__(self, ruta):
        self.d = docx.Document(ruta)
        self.body = self.d.element.body
        self.t_cuerpo = copy.deepcopy(self.buscar("En el Laboratorio 03 se analizó"))
        self.t_leyenda = copy.deepcopy(self.buscar("Figura 1."))
        self.t_codigo = copy.deepcopy(self.buscar("k <- r"))
        self.t_vineta = copy.deepcopy(self.buscar("Rango de 2^56 claves."))
        self.t_imagen = copy.deepcopy(self.buscar("Figura 1.").getprevious())
        self.t_tabla = copy.deepcopy(self.tabla_tras("Tiempos medidos con mpirun"))
        self._pendiente_espacio = []

    # --------------------------------------------------- busqueda
    def _parrafos(self):
        return [e for e in self.body.iterchildren() if e.tag == W_("p")]

    @staticmethod
    def texto(e):
        return "".join(t.text or "" for t in e.iter(W_("t")))

    def buscar(self, prefijo, desde=None):
        ok = desde is None
        for e in self.body.iterchildren():
            if not ok:
                ok = e is desde
                continue
            if e.tag == W_("p") and self.texto(e).strip().startswith(prefijo):
                return e
        raise KeyError(prefijo)

    def tabla_tras(self, prefijo):
        e = self.buscar(prefijo)
        while e is not None and e.tag != W_("tbl"):
            e = e.getnext()
        return e

    # --------------------------------------------------- runs
    @staticmethod
    def run(texto, fmt=""):
        r = etree.SubElement(etree.Element(W_("dummy")), W_("r"))
        rpr = etree.SubElement(r, W_("rPr"))
        if "b" in fmt:
            etree.SubElement(rpr, W_("b")); etree.SubElement(rpr, W_("bCs"))
        if "i" in fmt:
            etree.SubElement(rpr, W_("i")); etree.SubElement(rpr, W_("iCs"))
        if "c" in fmt:
            f = etree.SubElement(rpr, W_("rFonts"))
            for a in ("ascii", "hAnsi", "cs"):
                f.set(W_(a), "Consolas")
        if "c" in fmt or "s" in fmt:
            sz = "19" if "c" in fmt else "19"
            etree.SubElement(rpr, W_("sz")).set(W_("val"), sz)
            etree.SubElement(rpr, W_("szCs")).set(W_("val"), sz)
        if "p" in fmt:   # superindice
            etree.SubElement(rpr, W_("vertAlign")).set(W_("val"), "superscript")
        if len(rpr) == 0:
            r.remove(rpr)
        t = etree.SubElement(r, W_("t"))
        t.text = texto
        t.set("{http://www.w3.org/XML/1998/namespace}space", "preserve")
        return r

    @classmethod
    def runs(cls, contenido):
        """'texto' o [(texto, fmt), ...]. En un str, `x` es codigo y **x** negrita."""
        if isinstance(contenido, list):
            return [cls.run(t, f) for t, f in contenido]
        out = []
        for parte in re.split(r"(`[^`]+`|\*\*[^*]+\*\*)", contenido):
            if not parte:
                continue
            if parte.startswith("`"):
                out.append(cls.run(parte[1:-1], "c"))
            elif parte.startswith("**"):
                out.append(cls.run(parte[2:-2], "b"))
            else:
                out.append(cls.run(parte))
        return out

    def poner(self, p, contenido):
        """Reemplaza los runs de p (conserva pPr, marcadores y notas al pie si se indica)."""
        for hijo in list(p):
            if hijo.tag != W_("pPr") and hijo.tag not in (W_("bookmarkStart"), W_("bookmarkEnd")):
                p.remove(hijo)
        ancla = p.find(W_("bookmarkEnd"))
        for r in self.runs(contenido):
            if ancla is not None:
                ancla.addprevious(r)
            else:
                p.append(r)
        return p

    def reemplazar(self, p, viejo, nuevo):
        """Cambia texto dentro de los runs existentes (conserva su formato)."""
        ts = list(p.iter(W_("t")))
        completo = "".join(t.text or "" for t in ts)
        if viejo not in completo:
            raise KeyError(viejo)
        ts[0].text = completo.replace(viejo, nuevo)
        for t in ts[1:]:
            t.text = ""

    def nuevo(self, plantilla, contenido):
        p = copy.deepcopy(plantilla)
        return self.poner(p, contenido)

    def cuerpo(self, c):
        return self.nuevo(self.t_cuerpo, c)

    def leyenda(self, c):
        return self.nuevo(self.t_leyenda, [(c, "i")])

    def leyenda_tabla(self, c):
        p = self.nuevo(self.t_leyenda, [(c, "i")])
        ppr = p.find(W_("pPr"))
        ppr.insert(0, etree.Element(W_("keepNext")))
        sp = ppr.find(W_("spacing"))
        sp.set(W_("before"), "160"); sp.set(W_("after"), "60")
        return p

    def codigo(self, c):
        return self.nuevo(self.t_codigo, [(c, "c")])

    def vineta(self, negrita, resto):
        return self.nuevo(self.t_vineta, [(negrita, "b"), (resto, "")])

    def imagen(self, ruta, ancho_in):
        p = self.d.add_paragraph()
        p.add_run().add_picture(ruta, width=Inches(ancho_in))
        e = p._p
        e.getparent().remove(e)
        e.insert(0, copy.deepcopy(self.t_imagen.find(W_("pPr"))))
        return e

    @staticmethod
    def despues(ref, *nuevos):
        for n in nuevos:
            ref.addnext(n)
            ref = n
        return ref

    @staticmethod
    def quitar(*els):
        for e in els:
            e.getparent().remove(e)

    # --------------------------------------------------- tablas
    def tabla(self, encabezados, filas, anchos):
        """Tabla con el estilo del borrador. anchos en dxa (suman 9360)."""
        t = copy.deepcopy(self.t_tabla)
        filas_xml = t.findall(W_("tr"))
        f_enc, f_cuerpo = copy.deepcopy(filas_xml[0]), copy.deepcopy(filas_xml[1])
        for f in filas_xml:
            t.remove(f)
        grid = t.find(W_("tblGrid"))
        for g in list(grid):
            grid.remove(g)
        for a in anchos:
            etree.SubElement(grid, W_("gridCol")).set(W_("w"), str(a))
        t.find(W_("tblPr")).find(W_("tblW")).set(W_("w"), str(sum(anchos)))

        def fila(plantilla, valores, negrita):
            tr = copy.deepcopy(plantilla)
            tc0 = tr.findall(W_("tc"))[0]
            for tc in tr.findall(W_("tc")):
                tr.remove(tc)
            for v, a in zip(valores, anchos):
                tc = copy.deepcopy(tc0)
                tc.find(W_("tcPr")).find(W_("tcW")).set(W_("w"), str(a))
                p = tc.find(W_("p"))
                for r in p.findall(W_("r")):
                    p.remove(r)
                contenido = v if isinstance(v, list) else [(str(v), "bs" if negrita else "s")]
                for r in self.runs(contenido):
                    p.append(r)
                tr.append(tc)
            return tr

        t.append(fila(f_enc, encabezados, True))
        for vals in filas:
            t.append(fila(f_cuerpo, vals, False))
        self._pendiente_espacio.append(t)
        return t

    def espaciar_tablas(self):
        """Parrafo vacio despues de cada tabla nueva (como en el borrador)."""
        for t in self._pendiente_espacio:
            sig = t.getnext()
            if sig is None or sig.tag != W_("p") or self.texto(sig).strip():
                t.addnext(self.cuerpo(""))

    def guardar(self, ruta):
        self.d.save(ruta)


# =================================================================== contenido

def construir(D, datos, eq):
    d = D.d
    g = lambda prueba, prog, estr, p: datos[(prueba, prog, estr, p)]
    NP = 4
    nuc = eq["nucleos"]
    sobresus = nuc < NP

    # Tasas medidas (las corridas largas son las mas estables)
    v_seq = g("b", "secuencial", "-", 1)["tasa"]
    v_proc4 = statistics.mean([g("b", "mpi", "1", NP)["tasa_proc"], g("c", "mpi", "1", NP)["tasa_proc"]])
    v_evp = g("motor-evp", "secuencial", "-", 1)["tasa"]
    v_ni = g("motor-aesni", "secuencial", "-", 1)["tasa"]

    def intentos(letra, e):
        k = CLAVES[letra]
        n = 1 << 56
        if e == 1:
            return k % NP, k // NP + 1
        tam = n // NP
        return k // tam, k % tam + 1

    # ----------------------------------------------------------- caratula
    # en la portada se cambia solo el texto de los runs para conservar su formato
    D.reemplazar(D.buscar("CC3041"), "CC3041", "CC3069")
    D.reemplazar(D.buscar("Practica 02"), "Practica 02", "Proyecto 02")
    p = D.buscar("GUATEMALA, 14 de octubre")
    D.reemplazar(p, D.texto(p).strip(), FECHA)

    # ----------------------------------------------------------- 1. introduccion
    D.poner(D.buscar("En el Laboratorio 03 se analizó"),
            "En el Laboratorio 03 se analizó y mejoró un programa secuencial de búsqueda de claves AES "
            "(rango, clave y mensaje por parámetro, modo CBC con IV, reutilización del contexto de OpenSSL "
            "y validación de argumentos). Este proyecto parte de ese trabajo con tres objetivos: (1) cifrar y "
            "descifrar con AES-128 un texto leído desde un archivo `.txt`, usando una clave recibida como "
            "parámetro; (2) encontrar por fuerza bruta la clave usada, con 4 procesos MPI, midiendo el tiempo "
            "para tres claves de prueba; y (3) proponer, implementar y comparar una estrategia alternativa de "
            "distribución del rango de claves entre los procesos. Además, siguiendo el objetivo de optimizar el "
            "programa, se aceleró la prueba de cada candidata con las instrucciones AES-NI del procesador.")

    # ----------------------------------------------------------- 2. antecedentes (citas IEEE)
    def citar(p, numero):
        """Cambia la nota al pie de p por la cita [n] en el texto."""
        ref = p.find(".//" + W_("footnoteReference")).getparent()
        previo = ref.getprevious()
        ts = list(previo.iter(W_("t"))) if previo is not None else []
        if ts and ts[-1].text and ts[-1].text.rstrip().endswith("."):
            ts[-1].text = ts[-1].text.rstrip()[:-1] + f" [{numero}]."
        else:
            ref.addprevious(D.run(f" [{numero}]"))
        p.remove(ref)

    p = D.buscar("AES (Advanced Encryption Standard)")
    citar(p, 1)
    for t in p.iter(W_("t")):
        if "Vincent Rijmen." in (t.text or ""):
            t.text = t.text.replace("Vincent Rijmen.", "Vincent Rijmen [2].")
    p = D.buscar("Un texto de más de 16 bytes")
    citar(p, 3)
    for t in p.iter(W_("t")):
        if t.text and "El programa del laboratorio usa" in t.text:
            t.text = t.text.replace("El programa del laboratorio usa", "El programa del proyecto usa")
        if t.text and "API EVP de OpenSSL." in t.text:
            t.text = t.text.replace("API EVP de OpenSSL.", "API EVP de OpenSSL [4].")
    D.poner(D.buscar("El espacio completo de AES-128 tiene"),
            "El espacio completo de AES-128 tiene 2^128 ≈ 3.4×10^38 claves y es inviable de recorrer. El "
            "proyecto explora el rango [0, 2^56), es decir, N = 72 057 594 037 927 936 candidatas. El texto de "
            "prueba, \"Esta es una prueba de proyecto 2\", mide 32 bytes; como el relleno PKCS#7 agrega un "
            "bloque completo cuando el texto ya es múltiplo de 16, el cifrado ocupa 48 bytes (tres bloques). "
            "La frase de búsqueda ocupa los bytes 5 a 20 (contando desde 0) y cruza el límite entre el primer "
            "y el segundo bloque, por lo que no basta la prueba de un solo bloque del Laboratorio 03. Para que "
            "la prueba funcione con cualquier texto y cualquier frase, el programa descifra el mensaje "
            "completo con cada candidata y busca la frase en cualquier posición del resultado.")
    D.poner(D.buscar("MPI es el estándar de paso de mensajes"),
            "MPI es el estándar de paso de mensajes para memoria distribuida [5] y Open MPI es una de sus "
            "implementaciones. Un programa se lanza con varios procesos (`mpirun -np 4`); cada proceso conoce "
            "su identificador (rank) y el total (size) dentro del comunicador `MPI_COMM_WORLD`. En este "
            "proyecto se usan `MPI_Bcast` para difundir el mensaje cifrado, `MPI_Isend`, `MPI_Iprobe` y "
            "`MPI_Recv` para el aviso de clave encontrada (`MPI_Iprobe` revisa sin bloquearse si hay un "
            "mensaje pendiente [6]), `MPI_Barrier` antes de medir, `MPI_Allreduce` y `MPI_Reduce` para "
            "reunir resultados, `MPI_Waitall` para cerrar los envíos y `MPI_Wtime` para medir el tiempo.")
    p = D.buscar("El rendimiento se mide con el Speedup")
    citar(p, 7)

    # ----------------------------------------------------------- 3.1 programa base
    p31 = D.buscar("El punto de partida es el programa secuencial mejorado")
    D.poner(p31,
            "El punto de partida es el programa secuencial mejorado del Laboratorio 03 (rango, clave y mensaje "
            "por línea de comandos, AES-128-CBC con IV, reutilización del contexto de OpenSSL por candidata y "
            "validación de argumentos). Para el proyecto se reorganizó en tres programas que comparten las "
            "rutinas de `src/common/aes_texto.c` (Anexo 1): `aes_texto` cifra (`-e`) o descifra (`-d`) un "
            "archivo con la clave como parámetro; `busqueda_secuencial` recorre las candidatas con un solo "
            "proceso y es la referencia para el Speedup; y `busqueda_mpi` reparte la búsqueda entre procesos "
            "MPI con la estrategia 1 o 2 (`-e 1|2`). La Figura 1 muestra el flujo general.")
    fig1 = D.buscar("Figura 1.")
    D.poner(fig1, [("Figura 1. Flujo general: cifrar y descifrar (aes_texto) y buscar la clave "
                    "(busqueda_secuencial y busqueda_mpi).", "i")])
    D.despues(fig1,
              D.cuerpo("**Conversión de la clave y prueba de una candidata.** La clave numérica k se convierte "
                       "con `clave_desde_u64()` en 16 bytes: los bytes 0 a 7 valen cero y los bytes 8 a 15 "
                       "contienen k en big-endian (123456 → 0000000000000000000000000001E240). Cada candidata "
                       "se prueba con `probar_clave()`: se descifran con AES-128-CBC y el IV del mensaje los 48 "
                       "bytes del cifrado, sin validar todavía el relleno (con una clave incorrecta el último "
                       "bloque es basura y no debe tratarse como error), y `contiene_frase()` busca \"es una "
                       "prueba de\" en cualquier posición (`memchr` localiza el primer byte y `memcmp` compara "
                       "el resto). Solo si la frase aparece se verifica el relleno PKCS#7, lo que descarta "
                       "falsos positivos sin costo para las candidatas incorrectas. El contexto se prepara una "
                       "sola vez por proceso en `buscador_iniciar()` y, por candidata, solo cambia la clave."),
              D.cuerpo(f"**Optimización con AES-NI.** En OpenSSL 3 cada `EVP_DecryptInit_ex` reinicia el "
                       f"contexto del proveedor, y para un mensaje de tres bloques ese costo fijo domina. Por "
                       f"eso se agregó una segunda implementación de la prueba que expande la clave y descifra "
                       f"CBC directamente con las instrucciones AES-NI del procesador (`_mm_aeskeygenassist_si128`, "
                       f"`_mm_aesimc_si128`, `_mm_aesdec_si128`). Se elige en tiempo de ejecución "
                       f"(`-a auto|evp|aesni`): si la CPU no tiene AES-NI se usa EVP, y ambas rutas se verificaron "
                       f"contra OpenSSL. En el equipo de pruebas la tasa secuencial subió de "
                       f"{sci(v_evp)} a {sci(v_ni)} claves/s ({v_ni / v_evp:.1f} veces, Tabla 7). Todas las "
                       f"mediciones del capítulo 4 usan AES-NI."))

    # ----------------------------------------------------------- 3.2 / 3.3 pseudocodigo
    for p in D._parrafos():
        tx = D.texto(p)
        if "cada CHECK intentos: si hay_mensaje(FOUND)" in tx:
            D.poner(p, [("      cada 4096 candidatas: si hay_aviso_found(): terminar", "c")])
        elif "si probar(k): avisar_encontrada(k); terminar" in tx:
            D.poner(p, [(tx.replace("si probar(k)", "si probar_clave(k)"), "c")])
    D.poner(D.buscar("Con p procesos, el proceso de rango r prueba"),
            "Con p procesos, el proceso de rango r prueba las claves k = r, r + p, r + 2p, … menores que N "
            "(`calcular_tramo()` devuelve inicio = r y paso = p). Cada candidata pertenece a exactamente un "
            "proceso (el que cumple k mod p = r), así que ninguna se omite ni se repite. Todos los procesos "
            "avanzan a la vez sobre la misma zona del rango: la clave k se encuentra después de ⌊k/p⌋ + 1 "
            "intentos del proceso que la tiene, por lo que el tiempo es aproximadamente k / (p·v), donde v es "
            "la cantidad de claves por segundo de un proceso. Es la estrategia base: el Speedup es cercano a p "
            "sin importar dónde esté la clave, pero el tiempo crece con su posición.")

    # ----------------------------------------------------------- 3.4 terminacion
    p34 = D.buscar("Cuando un proceso encuentra la clave, la envía con MPI_Send")
    D.poner(p34,
            "Antes de buscar, el proceso 0 cifra el texto (o lee el archivo cifrado) y difunde el IV y el "
            "cifrado con `MPI_Bcast`, de modo que todos los procesos buscan exactamente el mismo mensaje. Los "
            "procesos se sincronizan con `MPI_Barrier` y cada uno toma el tiempo inicial con `MPI_Wtime`; así "
            "la medición no incluye el arranque de mpirun, la lectura del archivo ni la difusión.")
    D.despues(p34,
              D.cuerpo("Cuando un proceso encuentra la clave sale de su ciclo y la envía a cada uno de los demás "
                       "con `MPI_Isend` (etiqueta FOUND, función `avisar_encontrada()`). El envío es no "
                       "bloqueante: con `MPI_Send` el proceso podría quedarse esperando a un receptor que ya "
                       "está en las reducciones finales, lo cual el estándar permite y se comprobó que bloquea "
                       "el programa si se usa envío síncrono. Cada proceso revisa con `MPI_Iprobe` "
                       "(`hay_aviso_found()`), cada 4096 candidatas, si llegó un aviso; si llegó, lo recibe con "
                       "`MPI_Recv` y deja de buscar. En ese mismo punto se revisa el límite de tiempo de la "
                       "opción `-t`. Revisar cada 4096 candidatas hace despreciable el costo de la comunicación, "
                       "a cambio de que los demás procesos sigan probando al menos hasta su siguiente revisión "
                       "después del hallazgo; en las corridas, con más procesos que núcleos, el aviso tardó a veces "
                       "varios intervalos en llegar (el total de claves probadas del Anexo 2 lo muestra)."),
              D.cuerpo("Al salir del ciclo (por hallazgo, aviso, límite de tiempo o rango agotado) todos "
                       "participan en las reducciones: `MPI_Allreduce` con `MPI_MIN` obtiene la clave global "
                       "(UINT64_MAX significa \"no encontrada\") y el proceso que la halló; con `MPI_Reduce` el "
                       "proceso 0 reúne el tiempo del último proceso en detenerse (`MPI_MAX`) y el total de "
                       "claves probadas (`MPI_SUM`). Antes de `MPI_Finalize`, `recibir_avisos_pendientes()` "
                       "calcula con `MPI_Allreduce` cuántos procesos encontraron la clave, recibe exactamente los "
                       "avisos que faltan; después, en `main`, el proceso que avisó espera sus envíos con `MPI_Waitall`, de modo que no "
                       "queda ningún mensaje sin recibir. Si OpenSSL falla se llama a `MPI_Abort` para no dejar "
                       "procesos esperando. El proceso 0 imprime la clave, el proceso que la halló, sus "
                       "intentos y su tiempo, el texto recuperado, el tiempo total y la tasa; si la búsqueda se "
                       "cortó con `-t`, imprime la estimación del tiempo para la clave de referencia (Figura 3)."))
    # Figura 3 nueva
    img3 = D.buscar("Figura 3.").getprevious()
    nueva = D.imagen(FIG3, 3.9)
    img3.addprevious(nueva)
    D.quitar(img3)
    D.poner(D.buscar("Figura 3."), [("Figura 3. Flujo de cada proceso MPI: difusión del mensaje, búsqueda, "
                                     "aviso de la clave encontrada y cierre de la comunicación.", "i")])

    # ----------------------------------------------------------- 4. resultados
    p41 = D.buscar("[PENDIENTE: insertar aquí las capturas")
    D.poner(p41,
            f"Las Figuras 4 y 5 muestran el cifrado y el descifrado del archivo `datos/prueba.txt` con la clave "
            f"123456, convertida a 128 bits (0000000000000000000000000001e240). Se ven el texto original, el IV "
            f"aleatorio, el texto cifrado de 48 bytes (32 bytes del texto más un bloque de relleno PKCS#7) y el "
            f"texto recuperado, idéntico al original. La Figura 6 muestra que con otra clave (123457) el "
            f"relleno no es válido y el programa lo rechaza. Todas las pruebas se ejecutaron en: {eq['cpu']}, "
            f"{nuc} núcleos, {eq['so']}, {eq['mpi']} y {eq['ssl']} (Figura 7).")
    D.despues(p41,
              D.imagen(captura("02_cifrar.png"), 6.4),
              D.leyenda(f"Figura 4. Cifrado: {comando('02_cifrar.txt')}"),
              D.imagen(captura("03_descifrar.png"), 6.4),
              D.leyenda(f"Figura 5. Descifrado: {comando('03_descifrar.txt')}"),
              D.imagen(captura("03b_clave_incorrecta.png"), 6.4),
              D.leyenda("Figura 6. Descifrado con una clave incorrecta: el relleno PKCS#7 no es válido."))

    # 4.2 tabla de tiempos medidos
    p42 = D.buscar("Tiempos medidos con mpirun -np 4")
    nota_oversub = (f" El equipo tiene {nuc} núcleos, así que con `-np 4` se usó `--oversubscribe`: dos "
                    f"procesos comparten cada núcleo y la tasa por proceso es menor que la del secuencial."
                    if sobresus else "")
    D.poner(p42,
            f"La Tabla 3 resume los tiempos medidos con `mpirun -np 4` para el texto \"Esta es una prueba de "
            f"proyecto 2\" y la frase \"es una prueba de\" (promedio ± desviación estándar muestral de "
            f"{g('a', 'mpi', '1', NP)['n']} repeticiones cuando la búsqueda termina; cada repetición está en la Tabla 12). Las búsquedas que "
            f"no terminan en un tiempo razonable se cortaron con `-t` y se reporta la estimación que calcula "
            f"el programa a partir de la tasa medida. Las capturas están en las Figuras 9 a 17.{nota_oversub}")
    t_old = D.tabla_tras("La Tabla 3 resume los tiempos")
    filas = []
    for L in "abc":
        def celda(prog, e, p):
            x = g(L, prog, e, p)
            if x["est"]:
                return f"no termina; {t_txt(x['t'], True)} (est.)"
            return pm(x["t"], x["sd"]) if x["n"] > 1 else t_txt(x["t"])
        filas.append([NOMBRE_CLAVE[L], celda("mpi", "1", NP), celda("mpi", "2", NP), celda("secuencial", "-", 1)])
    t_new = D.tabla(["Clave", "Estrategia 1, -np 4", "Estrategia 2, -np 4", "Secuencial"], filas,
                    [2700, 2220, 2220, 2220])
    t_old.addprevious(D.leyenda_tabla("Tabla 3. Tiempo de búsqueda de cada clave (medido o estimado)."))
    t_old.addprevious(t_new)
    D.quitar(t_old)

    # 4.2 tabla de intentos / estimacion
    p_est = D.buscar("Para comparar con las mediciones")
    D.poner(p_est,
            f"La Tabla 4 indica cuántos intentos debe hacer el proceso que posee cada clave y el tiempo que "
            f"eso implica con la tasa medida en las corridas acotadas: v ≈ {sci(v_proc4)} claves/s por proceso "
            f"con 4 procesos en este equipo (el secuencial alcanza {sci(v_seq)} claves/s). La tasa de 2.7×10^6 "
            f"claves/s del Laboratorio 03 ya no aplica: aquel programa probaba un solo bloque con EVP y este "
            f"descifra tres bloques con AES-NI.")
    t_old = D.tabla_tras("La Tabla 4 indica cuántos intentos")
    filas = []
    for L in "abc":
        r1, n1 = intentos(L, 1)
        r2, n2 = intentos(L, 2)
        dueno = f"{r1}" if r1 == r2 else f"{r1} (e1), {r2} (e2)"
        filas.append([NOMBRE_CLAVE[L].split(" (")[0], dueno, miles(n1), miles(n2),
                      f"{t_txt(n1 / v_proc4, n1 / v_proc4 > 60)} / {t_txt(n2 / v_proc4)}"])
    t_new = D.tabla(["Clave", "Proceso dueño", "Intentos (estr. 1)", "Intentos (estr. 2)",
                     "Tiempo esperado (estr. 1 / estr. 2)"], filas, [1900, 1350, 2350, 1250, 2510])
    t_old.addprevious(D.leyenda_tabla("Tabla 4. Intentos del proceso dueño y tiempo esperado con la tasa medida."))
    t_old.addprevious(t_new)
    D.quitar(t_old)

    b1, c1 = g("b", "mpi", "1", NP), g("c", "mpi", "1", NP)
    lim = float(re.search(r"-t (\d+)", comando("06_busqueda_b_e1_r1.txt")).group(1))
    D.poner(D.buscar("La clave b) no puede medirse con la estrategia 1"),
            f"Las claves b) y c) no pueden medirse con la estrategia 1 ni con el secuencial: se cortaron a los "
            f"{lim:.0f} s ({b1['n']} repeticiones cada una). En ese tiempo los 4 procesos probaron unas "
            f"{sci(b1['tasa'] * lim)} (b) y {sci(c1['tasa'] * lim)} (c) claves sin encontrarla, y el programa estima {t_txt(b1['t'], True)} para "
            f"b) y {t_txt(c1['t'], True)} para c), pues el proceso dueño necesita 2^52 + 1 intentos en ambos "
            f"casos. Con la estrategia 2 las mismas claves se encontraron en {t_txt(g('b', 'mpi', '2', NP)['t'])} "
            f"y {t_txt(g('c', 'mpi', '2', NP)['t'])}: son la primera y la segunda clave del bloque del proceso 1.")

    # 4.3
    a1, a2, as_ = g("a", "mpi", "1", NP), g("a", "mpi", "2", NP), g("a", "secuencial", "-", 1)
    latencia = 4096 / v_proc4
    p114 = D.buscar("Las claves 2^54 y 2^54 + 1 difieren")
    for t in p114.iter(W_("t")):
        if t.text and "[PENDIENTE" in t.text:
            t.text = t.text[:t.text.index("[PENDIENTE")].rstrip() + " "
    for r in list(p114.findall(W_("r"))):       # runs que solo tenian la marca
        if D.texto(r).strip() == "" and r.find(W_("rPr")) is not None and \
                r.find(W_("rPr")).find(W_("highlight")) is not None:
            p114.remove(r)
    p_43a = D.buscar("En una búsqueda por fuerza bruta el tiempo no depende")
    D.reemplazar(p_43a, "123 456 pasos", "123\u00a0456 pasos")
    D.reemplazar(p_43a, "30 mil intentos", "30\u00a0mil intentos")
    D.reemplazar(p114, "pero sus tiempos serán prácticamente iguales dentro de cada estrategia",
                 "y sus tiempos resultaron del mismo orden dentro de cada estrategia, con diferencias que caen en el ruido de medición (Tabla 3)")
    p43 = D.cuerpo("")
    D.despues(p114, p43)
    D.poner(p43,
            f"Las mediciones confirman el análisis. Con la clave a) la estrategia 1 tardó {t_txt(a1['t'])} y la "
            f"2 tardó {t_txt(a2['t'])}, una razón de {a2['t'] / a1['t']:.1f} frente a la teórica de "
            f"123\u00a0457 / 30\u00a0865 ≈ 4.0" +
            (". La coincidencia es buena, aunque cada corrida varía bastante (ver las desviaciones de la "
             "Tabla 3) porque" if abs(a2["t"] / a1["t"] - 4.0) < 0.4 else
             ". La diferencia se explica porque") +
            f" en tiempos de milisegundos pesan efectos "
            f"que la cuenta de intentos no incluye: el tiempo reportado es el del último proceso en detenerse, "
            f"y los demás siguen probando al menos hasta su siguiente revisión, cada 4096 candidatas "
            f"(≈ {t_txt(latencia)} a la tasa medida), y a veces varios intervalos más mientras llega el aviso; "
            f"además, " +
            ("al haber más procesos que núcleos, el sistema operativo alterna los procesos y la velocidad de "
             "cada uno varía entre corridas." if sobresus else
             "el planificador del sistema operativo introduce variaciones entre corridas.") +
            f" Con b) y c) la diferencia entre estrategias es de unas {sci(b1['t'] / g('b', 'mpi', '2', NP)['t'])} "
            f"y {sci(c1['t'] / g('c', 'mpi', '2', NP)['t'])} veces, respectivamente: no por hacer el trabajo más rápido, sino por visitar primero la zona donde está la clave.")

    # 4.4 speedup
    D.poner(D.buscar("El Speedup se calcula como S = T_secuencial"),
            "El Speedup se calcula como S = T_secuencial / T_paralelo con 4 procesos y la eficiencia como "
            "E = S / 4 [7]. Cuando un tiempo no puede medirse se usa su estimación, y el Speedup se marca como "
            "estimado (est.). La Tabla 5 compara cada clave y estrategia con el secuencial.")
    t_old = D.tabla_tras("El Speedup se calcula como S")
    filas = []
    for L in "abc":
        ts = g(L, "secuencial", "-", 1)
        for e in "12":
            tp = g(L, "mpi", e, NP)
            s = ts["t"] / tp["t"]
            est = ts["est"] or tp["est"]
            filas.append([NOMBRE_CLAVE[L].split(" (")[0], t_txt(ts["t"], ts["est"]), t_txt(tp["t"], tp["est"]),
                          sci(s) + (" (est.)" if est else ""), sci(s / NP), e])
    t_new = D.tabla(["Clave", "T secuencial", "T paralelo", "Speedup", "Eficiencia", "Estrategia"], filas,
                    [2000, 1750, 1750, 1500, 1200, 1160])
    t_old.addprevious(D.leyenda_tabla("Tabla 5. Speedup y eficiencia de cada prueba con 4 procesos."))
    t_old.addprevious(t_new)
    ref = t_old.getnext()   # parrafo vacio tras la tabla
    D.quitar(t_old)

    # escalabilidad y motores
    bits = int(re.search(r"-n (\d+)", comando("*_escala_secuencial_r1.txt")).group(1))
    esc_s = g("escala", "secuencial", "-", 1)
    filas = [["Secuencial", "—", pm(esc_s["t"], esc_s["sd"]),
              f"{esc_s['tasa_proc']:,.0f}".replace(",", "\u00a0"), "1.00", "1.00"]]
    for p in (1, 2, 4):
        for e in "12":
            x = g("escala", "mpi", e, p)
            s = esc_s["t"] / x["t"]
            filas.append([str(p), e, pm(x["t"], x["sd"]), f"{x['tasa_proc']:,.0f}".replace(",", "\u00a0"),
                          f"{s:.2f}", f"{s / p:.2f}"])
    sp = {(p, e): esc_s["t"] / g("escala", "mpi", e, p)["t"] for p in (1, 2, 4) for e in "12"}
    rango = lambda p: f"{min(sp[(p, '1')], sp[(p, '2')]):.2f}–{max(sp[(p, '1')], sp[(p, '2')]):.2f}"
    txt_esc = (f"Los valores de Speedup de la Tabla 5 mezclan dos efectos: el paralelismo y el orden en que cada estrategia "
               f"recorre el rango. Los valores enormes de la estrategia 2 con b) y c) (del orden de "
               f"{sci(g('b', 'secuencial', '-', 1)['t'] / g('b', 'mpi', '2', NP)['t'])}) no son aceleración "
               f"paralela, sino que la clave queda al inicio de un bloque; y el Speedup menor que 1 de la "
               f"estrategia 2 con a) se debe a que solo el proceso 0 trabaja sobre las claves pequeñas. " +
               (f"El Speedup de {as_['t'] / a1['t']:.2f} de la estrategia 1 con a) supera levemente el límite de "
                f"{nuc} núcleos, pero con tiempos de pocos milisegundos la variación entre corridas es del mismo "
                f"orden (± {t_txt(a1['sd'])}), así que no debe leerse como un Speedup superlineal. "
                if sobresus and as_["t"] / a1["t"] > nuc else "") +
               f"Para "
               f"medir solo el paralelismo se repitió la búsqueda de la última clave de un rango de 2^{bits} "
               f"({miles((1 << bits) - 1)}), que en ambas estrategias es la última candidata del último proceso: "
               f"todos recorren su tramo completo y el trabajo es el mismo. El secuencial tardó "
               f"{t_txt(esc_s['t'])} (primera fila de la Tabla 6). Con 2 procesos el Speedup fue {rango(2)} y con "
               f"4 procesos {rango(4)} según la estrategia. Con 1 proceso el programa MPI es levemente más rápido "
               f"que el secuencial (S = {rango(1)}) y con 2 procesos la eficiencia llega a 1.0: el ciclo de "
               f"busqueda_mpi y el de busqueda_secuencial no son idénticos y la variación entre corridas es de "
               f"± {pm(esc_s['t'], esc_s['sd']).split('± ')[1]}, así que esas diferencias no son significativas. ")
    if sobresus:
        txt_esc += (f"Como el equipo solo tiene {nuc} núcleos, el Speedup no puede superar {nuc}: con 4 procesos "
                    f"cada núcleo se reparte entre dos procesos y la tasa por proceso cae a la mitad. En un "
                    f"equipo con 4 o más núcleos se espera un Speedup cercano a 4 con ambas estrategias.")
    else:
        txt_esc += "El Speedup crece casi linealmente con el número de procesos en ambas estrategias."
    D.despues(ref,
              D.cuerpo(txt_esc),
              D.leyenda_tabla(f"Tabla 6. Escalabilidad con trabajo fijo (rango 2^{bits}, clave 2^{bits} − 1)."),
              D.tabla(["Procesos", "Estrategia", "Tiempo", "Claves/s por proceso", "Speedup", "Eficiencia"],
                      filas, [1200, 1200, 2300, 2060, 1300, 1300]),
              D.cuerpo(f"La Tabla 7 compara las dos implementaciones de la prueba de candidatas en el programa "
                       f"secuencial con el mismo rango de 2^{bits} claves (Figuras 18 y 19)."),
              D.leyenda_tabla("Tabla 7. Tasa de la búsqueda secuencial según la implementación de AES."),
              D.tabla(["Implementación", "Tiempo", "Claves/s", "Aceleración"],
                      [["OpenSSL EVP (-a evp)", t_txt(g("motor-evp", "secuencial", "-", 1)["t"]),
                        f"{v_evp:,.0f}".replace(",", " "), "1.00"],
                       ["AES-NI (-a aesni)", t_txt(g("motor-aesni", "secuencial", "-", 1)["t"]),
                        f"{v_ni:,.0f}".replace(",", " "), f"{v_ni / v_evp:.2f}"]],
                      [3360, 2000, 2000, 2000]))

    # leyendas de las tablas del capitulo 2
    t1 = D.tabla_tras("AES (Advanced Encryption Standard)")
    t1.addprevious(D.leyenda_tabla("Tabla 1. Variantes de AES."))
    t2 = D.tabla_tras("La clave llega como un número entero")
    t2.addprevious(D.leyenda_tabla("Tabla 2. Claves de prueba convertidas a 128 bits."))

    # ----------------------------------------------------------- 5. discusion
    for prefijo, negrita, resto in [
        ("Frase de búsqueda entre dos bloques.", "Frase de búsqueda entre dos bloques.",
         " La frase de 16 bytes cruza el límite de bloque del texto y, en general, su posición es "
         "desconocida, así que cada candidata exige descifrar los tres bloques del mensaje. Esto reduce la "
         "tasa frente a la prueba de un solo bloque del laboratorio; se compensó con AES-NI."),
        ("Terminación coordinada.", "Terminación coordinada.",
         " Se resolvió con un aviso FOUND enviado con MPI_Isend y revisado con MPI_Iprobe cada 4096 "
         "candidatas. La primera versión usaba MPI_Send y vaciaba los avisos con un solo MPI_Iprobe; la "
         "revisión de código mostró que eso depende del buffer interno de MPI, así que ahora se cuentan y "
         "reciben exactamente los avisos pendientes."),
        ("Pruebas que no terminan.", "Pruebas que no terminan.",
         f" Las claves b) y c) con la estrategia 1 requieren 2^52 + 1 intentos ({t_txt(b1['t'], True)} en "
         f"este equipo); se reporta una estimación a partir de corridas acotadas con -t."),
        ("Sobrecarga en pruebas cortas.", "Mediciones cortas y ruidosas.",
         " Con 123456 el tiempo es de milisegundos y lo afectan la latencia de parada (una o más revisiones "
         "de 4096 candidatas por proceso), el reparto de núcleos del sistema operativo y que se reporte el tiempo máximo entre "
         "procesos; por eso cada medición se repitió y se reporta promedio y desviación."),
    ]:
        D.poner(D.buscar(prefijo), [(negrita, "b"), (resto, "")])
    D.despues(D.buscar("Mediciones cortas y ruidosas."),
              D.vineta("Rendimiento por candidata.", f" La API EVP de OpenSSL 3 tiene un costo fijo alto por "
                       f"clave; implementar la expansión de clave y el descifrado con AES-NI aumentó la tasa "
                       f"{v_ni / v_evp:.1f} veces sin perder portabilidad (se conserva EVP como respaldo)."))

    D.poner(D.buscar("La estrategia 1 mantiene un Speedup cercano a p"),
            [("La estrategia 1 reparte el trabajo de forma que todos los procesos colaboran en la misma zona "
              f"del rango: con la clave a) fue la más rápida ({t_txt(a1['t'])}), pero para b) y c) resulta "
              f"impracticable ({t_txt(b1['t'], True)}).", "")])
    D.poner(D.buscar("La estrategia 2 encuentra casi de inmediato"),
            [(f"La estrategia 2 encontró b) y c) en {t_txt(g('b', 'mpi', '2', NP)['t'])} y "
              f"{t_txt(g('c', 'mpi', '2', NP)['t'])} porque están al inicio de un bloque, pero con a) fue "
              f"{a2['t'] / a1['t']:.1f} veces más lenta que la estrategia 1, porque solo el proceso 0 recorre "
              f"las claves pequeñas.", "")])
    D.despues(D.buscar("[PENDIENTE: completar con los resultados medidos"),
              D.nuevo(D.buscar("El tiempo de una búsqueda por fuerza bruta depende"),
                      [("Como aprendizaje, el proyecto mostró que en una búsqueda paralela importa tanto el reparto "
                        "como la velocidad de cada proceso, que la terminación con MPI debe diseñarse según el "
                        "estándar y no según el comportamiento observado de una implementación, y que el Speedup "
                        "solo se interpreta bien si se separa el efecto del orden de recorrido del efecto del "
                        "paralelismo.", "")]))
    D.poner(D.buscar("[PENDIENTE: completar con los resultados medidos"),
            [(f"Con trabajo fijo, ambas estrategias escalan igual: el Speedup con 2 procesos fue {rango(2)} y con "
              f"4 procesos {rango(4)}" + (f", limitado por los {nuc} núcleos del equipo" if sobresus else "") +
              f". La optimización con AES-NI aportó otro factor de {v_ni / v_evp:.1f} independiente del número "
              f"de procesos.", "")])

    p = D.buscar("Si no se conoce dónde está la clave")
    fn = p.find(".//" + W_("footnoteReference")).getparent()
    for hijo in list(p):
        if hijo.tag != W_("pPr") and hijo is not fn:
            p.remove(hijo)
    fn.addprevious(D.run("Si no se conoce dónde está la clave, combinar ambas ideas: repartir el rango en "
                         "trozos pequeños bajo demanda desde un proceso coordinador, con varios puntos de "
                         "partida. Como señala [8, sec. 8.1], \"This approach can sometimes be an improvement "
                         "on the assignment of and [sic] equal number of tasks to all processes\"."))
    fn.addnext(D.run(" El costo es más comunicación con el coordinador, que se puede reducir con trozos "
                     "grandes."))
    D.poner(D.buscar("Aprovechar las instrucciones AES-NI"),
            [("Ejecutar las mediciones en un equipo con al menos 4 núcleos físicos (o en varias máquinas con "
              "Open MPI) para observar el Speedup completo; el programa no requiere cambios para eso.", "")])

    # ----------------------------------------------------------- anexo 1
    p = D.buscar("Funciones del programa base")
    D.poner(p, [("Rutinas compartidas", "b"), (" (include/aes_texto.h, src/common/aes_texto.c), usadas por los "
                                               "tres programas:", "")])
    rutinas = [
        ("reportar_error", "mensaje", "—", "Imprime el error y la cola de errores de OpenSSL (no termina el programa)."),
        ("reloj", "—", "segundos (double)", "Reloj monótono (CLOCK_MONOTONIC) para el secuencial."),
        ("parse_u64_seguro", "texto, *valor", "0 / -1", "Convierte decimal o 0x… a uint64_t; rechaza signos, espacios y desbordamientos."),
        ("parse_clave_hex, valor_hex", "texto, clave[16]", "0 / -1", "Lee una clave o IV de 32 dígitos hexadecimales (valor_hex convierte un dígito)."),
        ("imprimir_hex, imprimir_texto", "datos, n", "—", "Muestran bytes en hex o como texto con escapes (\\n, \\xNN)."),
        ("imprimir_duracion", "segundos", "—", "Imprime una duración y su equivalente en horas, días o años."),
        ("leer_archivo", "ruta, máximo, *buffer", "0 / -1", "Lee un archivo completo por bloques con límite de tamaño; valida apertura, lectura y archivo vacío."),
        ("escribir_archivo", "ruta, datos, n", "0 / -1", "Escribe un archivo y verifica fwrite y fclose."),
        ("buffer_liberar", "*buffer", "—", "Libera un buffer_t y lo deja vacío (seguro llamarla dos veces)."),
        ("clave_desde_u64", "k, clave[16]", "clave", "Convierte el entero k en 16 bytes big-endian con ceros a la izquierda."),
        ("cifrar_texto", "clave, IV, texto, n, salida", "longitud / -1", "AES-128-CBC con relleno PKCS#7 (EVP)."),
        ("descifrar_texto", "clave, IV, cifrado, n, salida", "longitud / -1", "Descifra y valida el relleno; -1 si la clave es incorrecta."),
        ("contiene_frase", "texto, n, frase, m", "1 / 0", "Busca la frase en cualquier posición (memchr + memcmp)."),
        ("aesni_disponible", "—", "1 / 0", "Detecta en tiempo de ejecución si la CPU tiene AES-NI y SSE4.1."),
        ("expandir_paso, subclaves_descifrado", "clave[16], dk[11]", "subclaves", "Expansión de clave de AES-128 con AES-NI y subclaves de descifrado (InvMixColumns)."),
        ("descifrar_cbc_aesni", "clave, IV, cifrado, n, salida", "—", "Descifra CBC bloque a bloque con aesdec/aesdeclast: P_i = D(C_i) xor C_(i-1)."),
        ("nombre_motor, nombre_estrategia", "motor o estrategia", "texto", "Nombre legible para los reportes."),
        ("buscador_iniciar", "*b, IV, cifrado, n, frase, m, motor", "0 / -1", "Prepara la prueba de candidatas: elige EVP o AES-NI, crea el contexto y el buffer de trabajo."),
        ("probar_clave", "*b, k", "1 / 0 / -1", "Descifra el mensaje con la clave k, busca la frase y, si aparece, valida el relleno."),
        ("relleno_valido", "texto, n", "1 / 0", "Revisa que el último bloque termine con un relleno PKCS#7 válido."),
        ("buscador_liberar", "*b", "—", "Libera el contexto EVP y el buffer."),
        ("calcular_tramo", "estrategia, N, p, rank", "tramo_t", "Inicio, paso y cantidad de claves del proceso (intercalado o bloques con residuo)."),
        ("ubicar_clave", "estrategia, N, p, k", "rank, intentos", "Qué proceso tiene la clave k y cuántos intentos le toma; para estimar tiempos."),
        ("parse_opciones", "argc, argv, …", "0 / -1", "Valida las opciones -i -k -c -f -n -t -e -a de los programas de búsqueda."),
        ("preparar_mensaje", "opciones, *mensaje", "0 / -1", "Cifra -i con -k (IV con RAND_bytes) o lee -c (IV + cifrado)."),
        ("mensaje_liberar", "*mensaje", "—", "Libera el mensaje cifrado."),
    ]
    t_old = D.tabla_tras("Rutinas compartidas")
    t_old.addprevious(D.leyenda_tabla("Tabla 8. Rutinas compartidas del proyecto."))
    t_old.addprevious(D.tabla(["Función", "Entradas", "Salida", "Propósito"], rutinas, [1900, 2360, 1500, 3600]))
    D.quitar(t_old)

    p = D.buscar("Funciones nuevas del proyecto")
    D.poner(p, [("Funciones de cada programa", "b"), (" (src/proyecto02):", "")])
    funcs = [
        ("uso (en cada programa)", "nombre del programa", "—", "Muestra la forma de uso cuando faltan o sobran argumentos."),
        ("aes_texto: leer_clave", "texto, clave[16]", "0 / -1", "Acepta la clave como entero (123456, 0x1e240) o hex:<32 dígitos>."),
        ("aes_texto: mostrar_hex, mostrar_texto", "etiqueta, datos, n", "—", "Imprimen un campo en hex (máx. 96 B) o como texto, con su tamaño."),
        ("aes_texto: cifrar_archivo", "clave, IV, texto, salida", "0 / -1", "Cifra y guarda IV || cifrado; muestra texto original y cifrado."),
        ("aes_texto: descifrar_archivo", "clave, archivo, salida", "0 / -1", "Separa el IV, descifra, valida el relleno y guarda el texto recuperado."),
        ("aes_texto: main", "-e/-d/-r CLAVE -i -o [-v IV]", "código de salida", "Modos cifrar, descifrar e ida y vuelta (verifica que el texto recuperado sea idéntico)."),
        ("busqueda_secuencial: main", "-i -k | -c, -f -n -t -a", "código de salida", "Recorre k = 0, 1, 2, … por lotes de 4096 revisando el límite de tiempo; reporta clave, texto, tiempo y tasa."),
        ("busqueda_mpi: difundir_mensaje", "opciones, rank, *mensaje", "0 / -1", "El proceso 0 prepara el mensaje y lo difunde con MPI_Bcast; si algún proceso no puede reservar memoria, todos devuelven -1 juntos (MPI_Allreduce con MPI_LAND)."),
        ("busqueda_mpi: buscar_en_tramo", "*b, tramo, límite, inicio, *r", "— (llena *r)", "Recorre el tramo del proceso; cada 4096 candidatas revisa avisos y el límite de tiempo."),
        ("busqueda_mpi: avisar_encontrada", "*k, rank, size, reqs", "nº de envíos", "Envía la clave a los demás procesos con MPI_Isend (etiqueta FOUND)."),
        ("busqueda_mpi: hay_aviso_found", "*k", "1 / 0", "MPI_Iprobe sin bloquear; si hay aviso lo recibe con MPI_Recv."),
        ("busqueda_mpi: recibir_avisos_pendientes", "resultado local", "—", "Cuenta los avisos enviados (MPI_Allreduce) y recibe los que faltan antes de MPI_Finalize."),
        ("busqueda_mpi: imprimir_encabezado, imprimir_clave", "opciones, mensaje, …", "—", "Reporte del proceso 0: estrategia, motor, clave, proceso dueño, intentos y texto."),
        ("busqueda_mpi: main", "-i -k | -c, -e -f -n -t -a", "código de salida", "Inicializa MPI, reparte, busca, reduce resultados (clave, tiempo máximo, claves probadas) y reporta."),
    ]
    t_old = D.tabla_tras("Funciones de cada programa")
    t_old.addprevious(D.leyenda_tabla("Tabla 9. Funciones de los programas del proyecto."))
    t_old.addprevious(D.tabla(["Función", "Entradas", "Salida", "Propósito"], funcs, [2500, 2160, 1400, 3300]))
    D.quitar(t_old)

    libs = [
        ("OpenSSL (EVP)", "EVP_CIPHER_CTX_new/free, EVP_EncryptInit_ex, EVP_DecryptInit_ex, EVP_EncryptUpdate, "
                          "EVP_DecryptUpdate, EVP_EncryptFinal_ex, EVP_DecryptFinal_ex, EVP_CIPHER_CTX_set_padding, "
                          "EVP_aes_128_cbc: cifrado y descifrado AES-128-CBC."),
        ("OpenSSL (rand, err)", "RAND_bytes: IV aleatorio. ERR_print_errors_fp, ERR_clear_error: errores de OpenSSL."),
        ("AES-NI (immintrin.h)", "_mm_aeskeygenassist_si128, _mm_aesimc_si128, _mm_aesdec_si128, _mm_aesdeclast_si128, "
                                 "_mm_shuffle_epi32, _mm_slli_si128, _mm_loadu/storeu/xor_si128; __builtin_cpu_init y "
                                 "__builtin_cpu_supports para detectar la CPU."),
        ("Open MPI", "MPI_Init, MPI_Comm_rank, MPI_Comm_size, MPI_Bcast, MPI_Barrier, MPI_Wtime, MPI_Isend, "
                     "MPI_Iprobe, MPI_Recv, MPI_Waitall, MPI_Allreduce, MPI_Reduce, MPI_Abort, MPI_Finalize."),
        ("Biblioteca estándar de C", "stdio.h, stdlib.h, string.h, stdint.h, inttypes.h, ctype.h, errno.h, math.h, "
                                     "limits.h, time.h (clock_gettime) y unistd.h (getopt)."),
    ]
    t_old = D.tabla_tras("Librerías:")
    t_old.addprevious(D.leyenda_tabla("Tabla 10. Librerías y funciones externas usadas."))
    t_old.addprevious(D.tabla(["Librería", "Funciones usadas y propósito"], libs, [2300, 7060]))
    D.quitar(t_old)

    # ----------------------------------------------------------- anexo 2
    p = D.buscar("Cada prueba debe incluir la captura")
    D.poner(p, f"Todas las pruebas se generaron con `scripts/bitacora.sh` (`make bitacora`), que guarda la "
               f"salida exacta de cada comando en `resultados/salidas/`, los tiempos en `resultados/tiempos.csv` "
               f"y las capturas de este anexo. Equipo: {eq['cpu']}, {nuc} núcleos, {eq['so']}, {eq['mpi']}, "
               f"{eq['ssl']}. La Tabla 11 resume cada prueba con la figura que la evidencia.")
    bit = []
    def fila_b(no, prueba, cmd, x, fig):
        res = x["resultado"].replace("CLAVE ENCONTRADA", "Encontrada").replace(
            "NO ENCONTRADA (limite de", "Cortada a").replace("NO ENCONTRADA (se alcanzo el limite de", "Cortada a")
        res = res.replace(" s alcanzado)", " s").replace(" s)", " s")
        tiempo = (f"{x['medido']:.3f} medido; {t_txt(x['t'], True)} est." if x["est"]
                  else f"{x['t']:.4f} ± {x['sd']:.4f}")
        bit.append([str(no), [(prueba + ": ", "s"), (cmd, "cs")], res, tiempo, fig])
    bit.append(["1", [("Compilación: ", "s"), ("make", "cs")], "Sin advertencias", "—", "Figura 8"])
    bit.append(["2", [("Cifrar: ", "s"), (comando("02_cifrar.txt"), "cs")], "Cifrado de 48 B", "—", "Figura 4"])
    bit.append(["3", [("Descifrar: ", "s"), (comando("03_descifrar.txt"), "cs")], "Texto idéntico", "—", "Figura 5"])
    no, fig = 4, 9
    for L, base in (("a", 4), ("b", 6), ("c", 8)):
        for e in "12":
            idx = base + int(e) - 1
            cmd = comando(f"{idx:02d}_busqueda_{L}_e{e}_r1.txt").replace("--allow-run-as-root ", "")
            fila_b(no, f"Búsqueda {L}), estrategia {e}", cmd, g(L, "mpi", e, NP), f"Figura {fig}")
            no += 1; fig += 1
    for L, idx in (("a", 10), ("b", 11), ("c", 12)):
        fila_b(no, f"Secuencial {L})", comando(f"{idx:02d}_secuencial_{L}_r1.txt"), g(L, "secuencial", "-", 1),
               f"Figura {fig}")
        no += 1; fig += 1
    x = esc_s
    bit.append([str(no), [("Escalabilidad (Tabla 6): ", "s"), (comando("*_escala_secuencial_r1.txt"), "cs"),
                          (" y las mismas con mpirun -np 1, 2, 4 y -e 1|2", "s")],
                "Encontrada", f"{x['t']:.3f} ± {x['sd']:.3f} (sec.)", "resultados/salidas/"])
    no += 1
    for m, fig_m in (("evp", fig), ("aesni", fig + 1)):
        x = g(f"motor-{m}", "secuencial", "-", 1)
        bit.append([str(no), [(f"Motor {m}: ", "s"), (comando(f"*_motor_{m}_r1.txt"), "cs")], "Encontrada",
                    f"{x['t']:.3f} ± {x['sd']:.3f}", f"Figura {fig_m}"])
        no += 1
    t_old = D.tabla_tras("Todas las pruebas se generaron")
    t_old.addprevious(D.leyenda_tabla("Tabla 11. Bitácora de pruebas (tiempos en segundos: promedio ± desviación)."))
    t_old.addprevious(D.tabla(["No.", "Prueba y comando", "Resultado", "Tiempo (s)", "Captura"], bit,
                              [600, 4500, 1400, 1700, 1160]))
    D.quitar(t_old)

    p = D.buscar("Cálculo del Speedup: S = T_secuencial")
    D.poner(p, "Cálculo del Speedup: S = T_secuencial / T_paralelo y E = S / p, con p = 4 (Tabla 5). Por "
               "ejemplo, para la clave a) con la estrategia 1: S = "
               f"{t_txt(as_['t'])} / {t_txt(a1['t'])} = {as_['t'] / a1['t']:.2f} y E = "
               f"{as_['t'] / a1['t'] / 4:.2f}. La Tabla 12 muestra cada repetición.")
    reps = []
    for (prueba, prog, e, pr), x in sorted(datos.items(), key=lambda kv: (kv[0][0], kv[0][1] != "secuencial", kv[0][2], kv[0][3])):
        if prueba not in "abc" or len(prueba) != 1:
            continue
        vals = "; ".join(f"{float(f['tiempo_s']):.4f}" for f in x["filas"])
        reps.append([f"{prueba})", "secuencial" if prog == "secuencial" else f"MPI e{e}", str(pr), vals,
                     (f"{t_txt(x['t'], True)} (est.)" if x["est"] else f"{x['t']:.4f} ± {x['sd']:.4f}")])
    figuras = [
        ("00_equipo.png", "Figura 7. Equipo y versiones usadas en las pruebas."),
        ("01_compilacion.png", "Figura 8. Compilación con make (mpicc para busqueda_mpi)."),
    ]
    fig = 9
    for L, base in (("a", 4), ("b", 6), ("c", 8)):
        for e in "12":
            idx = base + int(e) - 1
            figuras.append((f"{idx:02d}_busqueda_{L}_e{e}_r1.png",
                            f"Figura {fig}. Búsqueda de la clave {NOMBRE_CLAVE[L][3:]} con la estrategia {e} y 4 procesos."))
            fig += 1
    for L, idx in (("a", 10), ("b", 11), ("c", 12)):
        figuras.append((f"{idx:02d}_secuencial_{L}_r1.png",
                        f"Figura {fig}. Búsqueda secuencial de la clave {NOMBRE_CLAVE[L][3:]}."))
        fig += 1
    figuras.append(("*_motor_evp_r1.png", f"Figura {fig}. Búsqueda secuencial con OpenSSL EVP (-a evp)."))
    figuras.append(("*_motor_aesni_r1.png", f"Figura {fig + 1}. Búsqueda secuencial con AES-NI (-a aesni)."))
    nuevos = [D.leyenda_tabla("Tabla 12. Tiempo de cada repetición (s)."),
              D.tabla(["Clave", "Programa", "P", "Repeticiones (s)", "Promedio"], reps,
                      [900, 1400, 500, 4060, 2500])]
    for archivo, ley in figuras:
        nuevos += [D.imagen(captura(archivo), 6.4), D.leyenda(ley)]
    D.despues(p, *nuevos)

    # ----------------------------------------------------------- referencias
    refs = [
        "National Institute of Standards and Technology, \"Advanced Encryption Standard (AES),\" FIPS PUB 197, "
        "nov. 2001 (act. 2023). doi: 10.6028/NIST.FIPS.197-upd1.",
        "J. Daemen y V. Rijmen, The Design of Rijndael: AES – The Advanced Encryption Standard. Berlín, "
        "Alemania: Springer, 2002. doi: 10.1007/978-3-662-04722-4.",
        "M. Dworkin, \"Recommendation for Block Cipher Modes of Operation: Methods and Techniques,\" NIST SP "
        "800-38A, dic. 2001. doi: 10.6028/NIST.SP.800-38A.",
        "OpenSSL Project, \"EVP_EncryptInit,\" OpenSSL 3.0 Manual. [En línea]. Disponible: "
        "https://docs.openssl.org/3.0/man3/EVP_EncryptInit/",
        "Message Passing Interface Forum, MPI: A Message-Passing Interface Standard, Version 4.1, nov. 2023. "
        "[En línea]. Disponible: https://www.mpi-forum.org/docs/mpi-4.1/mpi41-report.pdf",
        "Open MPI Project, \"MPI_Iprobe(3)\" y \"MPI_Isend(3),\" Open MPI Documentation. [En línea]. "
        "Disponible: https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Iprobe.3.html",
        "P. S. Pacheco, An Introduction to Parallel Programming. Burlington, MA, EE. UU.: Morgan Kaufmann, 2011.",
        "Macalester College (CSinParallel), \"8.1 Dynamic Load Balancing,\" en Distributed Computing "
        "Fundamentals Using Python. [En línea]. Disponible: "
        "https://selkie.macalester.edu/DistributedPython/08DynamicLoadBalance/17dynamicLoad.html",
    ]
    p_ref = D.buscar("[1] NIST (2001)")
    viejas = []
    e = p_ref
    while e is not None and e.tag == W_("p") and D.texto(e).startswith("["):
        viejas.append(e)
        e = e.getnext()
    plantilla = copy.deepcopy(p_ref)
    for v in viejas:
        D.quitar(v)
    ancla = [x for x in D._parrafos() if D.texto(x).strip() == "Referencias"][-1]
    D.despues(ancla, *[D.nuevo(plantilla, [(f"[{i}] ", ""), (r, "")]) for i, r in enumerate(refs, 1)])

    # no debe quedar ningun pendiente
    resto = [D.texto(x) for x in D.body.iter(W_("p")) if "PEND" in D.texto(x)]
    if resto:
        sys.exit("quedaron pendientes: " + " | ".join(resto))


# =================================================================== partes del paquete

def ajustar_paquete(D):
    d = D.d
    pk = d.part.package
    # nota al pie de la cita textual: traduccion propia; las demas se eliminan
    fn = next(p for p in pk.iter_parts() if str(p.partname) == "/word/footnotes.xml")
    raiz = etree.fromstring(fn.blob)
    usadas = {e.get(W_("id")) for e in D.body.iter(W_("footnoteReference"))}
    for f in raiz.findall(W_("footnote")):
        fid = f.get(W_("id"))
        if f.get(W_("type")) in ("separator", "continuationSeparator"):
            continue
        if fid not in usadas:
            raiz.remove(f)
            continue
        ts = f.findall(".//" + W_("t"))
        ts[0].text = (" Traducción propia: «este enfoque a veces puede ser una mejora respecto de asignar el "
                      "mismo número de tareas a todos los procesos» [8].")
        for t in ts[1:]:
            t.getparent().remove(t)
    fn._blob = etree.tostring(raiz, xml_declaration=True, encoding="UTF-8", standalone=True)

    # numero de pagina en el pie (sin numero en la caratula)
    sect = d.sections[0]
    pie = sect.footer.paragraphs[0]._p
    for fld, texto in (("begin", None), (None, " PAGE "), ("separate", None), (None, "1"), ("end", None)):
        r = etree.SubElement(pie, W_("r"))
        if fld:
            etree.SubElement(r, W_("fldChar")).set(W_("fldCharType"), fld)
        elif texto == " PAGE ":
            it = etree.SubElement(r, W_("instrText"))
            it.text = texto
            it.set("{http://www.w3.org/XML/1998/namespace}space", "preserve")
        else:
            etree.SubElement(r, W_("t")).text = texto
    sect.different_first_page_header_footer = True

    # hipervinculos sin usar (restos de otra plantilla)
    doc_xml = etree.tostring(D.body).decode()
    for rid, rel in list(d.part.rels.items()):
        if rel.is_external and rid not in doc_xml:
            del d.part.rels[rid]
    cp = d.core_properties
    cp.author = "Javier Benítez, Javier López, Pedro Ávila"
    cp.title = "Proyecto 02 - Búsqueda de claves AES-128 con Open MPI"


# =================================================================== indice y PDF

def a_pdf(docx_path, carpeta):
    subprocess.run(["soffice", "--headless", "--convert-to", "pdf", "--outdir", carpeta, docx_path],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(carpeta, os.path.splitext(os.path.basename(docx_path))[0] + ".pdf")


def paginas_de_titulos(pdf):
    out = subprocess.run(["pdftotext", "-layout", pdf, "-"], capture_output=True, text=True, check=True).stdout
    return out.split("\f")


def actualizar_indice(ruta_docx, pdf):
    paginas = paginas_de_titulos(pdf)
    d = docx.Document(ruta_docx)
    cambios = 0
    for h in d.element.body.iter(W_("hyperlink")):
        if not (h.get(W_("anchor")) or "").startswith("_Toc"):
            continue
        ts = list(h.iter(W_("t")))
        titulo = ts[0].text.strip()
        # pagina donde el titulo aparece al inicio de una linea, saltando la del indice
        num = None
        for i, pag in enumerate(paginas):
            if i < 2:
                continue
            if re.search(r"^\s*" + re.escape(titulo) + r"\s*$", pag, re.M):
                num = i + 1
                break
        if num is not None and ts[-1].text != str(num):
            ts[-1].text = str(num)
            cambios += 1
    d.save(ruta_docx)
    return cambios


# =================================================================== estrategia alternativa

BORRADOR_ESTR = os.path.join(RAIZ, "docs/proyecto02/borrador/Estrategia_Alternativa_Proyecto02_borrador.docx")
SALIDA_ESTR = os.path.join(RAIZ, "docs/proyecto02/Estrategia_Alternativa_Proyecto02.docx")


def generar_estrategia(datos):
    """Actualiza el documento de la estrategia alternativa con el codigo final y los tiempos medidos."""
    D = Doc.__new__(Doc)
    D._pendiente_espacio = []
    D.d = docx.Document(BORRADOR_ESTR)
    D.body = D.d.element.body
    g = lambda prueba, prog, estr, p: datos[(prueba, prog, estr, p)]

    def vineta(prefijo, resto):
        p = D.buscar(prefijo)
        primero = p.find(W_("r"))
        negrita = primero is not None and primero.find(W_("rPr")) is not None and \
            primero.find(W_("rPr")).find(W_("b")) is not None
        D.poner(p, [(prefijo, "b" if negrita else ""), (resto, "")])

    vineta("Buscar:", " cada proceso prueba las claves de su bloque en orden ascendente. Para cada candidata "
           "la convierte a 16 bytes, descifra el mensaje completo (48 bytes, tres bloques) y busca la frase "
           "\"es una prueba de\" en cualquier posición; si aparece, confirma con el relleno PKCS#7.")
    vineta("Avisar:", " el proceso que encuentra la clave la envía a cada uno de los demás con MPI_Isend "
           "(mensaje FOUND), sin bloquearse.")
    vineta("Terminar:", " los demás revisan con MPI_Iprobe, cada 4096 candidatas y sin bloquearse, si llegó el "
           "aviso; si llegó, lo reciben y dejan de buscar. Si nadie la encuentra, cada proceso termina su bloque "
           "y el proceso 0 informa que la clave no fue encontrada.")
    for p in D.body.iter(W_("p")):
        tx = D.texto(p)
        if "si probar(k): enviar FOUND" in tx:
            D.reemplazar(p, "si probar(k): enviar FOUND a los demás", "si probar_clave(k): MPI_Isend FOUND a los demás")
        if "cada 4096 intentos: si hay FOUND" in tx:
            D.reemplazar(p, "cada 4096 intentos: si hay FOUND: recibir", "cada 4096 candidatas: si MPI_Iprobe(FOUND): recibir")
    p = D.buscar("La búsqueda es independiente")
    D.poner(p, "La búsqueda es independiente: los procesos no se comunican mientras prueban claves. Antes de "
               "empezar, el proceso 0 difunde el mensaje cifrado con MPI_Bcast y todos se sincronizan con "
               "MPI_Barrier para medir desde el mismo instante. Durante la búsqueda la única comunicación es el "
               "aviso FOUND; al final, MPI_Allreduce y MPI_Reduce reúnen la clave, el proceso que la halló, el "
               "tiempo máximo y las claves probadas, y se reciben los avisos pendientes antes de MPI_Finalize. "
               "La Figura 1 compara ambos repartos.")
    p = D.buscar("En una búsqueda por fuerza bruta el tiempo depende")
    D.reemplazar(p, " [PENDIENTE: confirmar con el integrante que hizo el MPI del Laboratorio 03 que su reparto es el intercalado]", "")
    D.reemplazar(p, "En la estrategia del inciso 2, el reparto intercalado,", "En la estrategia base del inciso 2, el reparto intercalado,")

    v = statistics.mean([g("b", "mpi", "1", 4)["tasa_proc"], g("c", "mpi", "1", 4)["tasa_proc"]])
    p = D.buscar("Con bloques, el tiempo depende")
    D.reemplazar(p, "La tabla resume los intentos del proceso que posee cada clave y el tiempo estimado con "
                    "v ≈ 2.7×10^6 claves/s por proceso, tasa medida en el Laboratorio 03.",
                 f"La tabla resume los intentos del proceso que posee cada clave y los tiempos medidos con "
                 f"-np 4; cuando la búsqueda no termina se indica la estimación con la tasa medida "
                 f"v ≈ {sci(v)} claves/s por proceso.")
    t = D.tabla_tras("Con bloques, el tiempo depende")
    filas = t.findall(W_("tr"))
    enc = filas[0].findall(W_("tc"))[-1]
    D.reemplazar(enc.find(W_("p")), D.texto(enc), "Tiempo medido con -np 4 (intercalada / bloques)")
    for fila, L in zip(filas[1:], "abc"):
        tc = fila.findall(W_("tc"))[-1]
        x1, x2 = g(L, "mpi", "1", 4), g(L, "mpi", "2", 4)
        t1 = f"no termina ({t_txt(x1['t'], True)})" if x1["est"] else t_txt(x1["t"])
        D.reemplazar(tc.find(W_("p")), D.texto(tc), f"{t1} / {t_txt(x2['t'])}")
    p = D.buscar("Por eso la estrategia por bloques reduce")
    b1 = g("b", "mpi", "1", 4)
    lim = float(re.search(r"-t (\d+)", comando("06_busqueda_b_e1_r1.txt")).group(1))
    D.poner(p, f"Por eso la estrategia por bloques reduce el tiempo de búsqueda para las claves cercanas al "
               f"inicio de algún bloque, como las de las pruebas b) y c): pasa de {t_txt(b1['t'], True)} "
               f"(estimado; la búsqueda se cortó a los {lim:.0f} s sin encontrarla) a "
               f"{t_txt(g('b', 'mpi', '2', 4)['t'])} y {t_txt(g('c', 'mpi', '2', 4)['t'])}.")
    p = D.buscar("Limitación. La mejora no es general.")
    a1, a2 = g("a", "mpi", "1", 4), g("a", "mpi", "2", 4)
    D.reemplazar(p, "el reparto intercalado es unas 4 veces más rápido,",
                 f"el reparto intercalado es unas 4 veces más rápido en teoría ({t_txt(a1['t'])} frente a "
                 f"{t_txt(a2['t'])} medidos),")
    p = D.buscar("GUATEMALA, 14 de octubre")
    D.reemplazar(p, D.texto(p).strip(), FECHA)
    resto = [D.texto(x) for x in D.body.iter(W_("p")) if "PENDIENTE" in D.texto(x)]
    if resto:
        sys.exit("quedaron pendientes en la estrategia: " + " | ".join(resto))
    D.d.save(SALIDA_ESTR)
    return SALIDA_ESTR


def main():
    datos = cargar_datos()
    eq = equipo()
    D = Doc(BORRADOR)
    construir(D, datos, eq)
    D.espaciar_tablas()
    ajustar_paquete(D)
    D.guardar(SALIDA)
    carpeta = os.path.dirname(SALIDA)
    for _ in range(3):           # el indice se estabiliza en una o dos pasadas
        pdf = a_pdf(SALIDA, carpeta)
        if actualizar_indice(SALIDA, pdf) == 0:
            break
    pdf = a_pdf(SALIDA, carpeta)
    estr = generar_estrategia(datos)
    a_pdf(estr, carpeta)
    print("Informe:   ", SALIDA)
    print("PDF:       ", pdf)
    print("Estrategia:", estr)


if __name__ == "__main__":
    main()
