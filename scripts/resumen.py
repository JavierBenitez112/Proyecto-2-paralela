#!/usr/bin/env python3
"""Resume resultados/tiempos.csv (generado por scripts/bitacora.sh) en tablas
Markdown: promedio y desviacion por prueba, Speedup S = T_sec / T_par y
eficiencia E = S / p.

Para las busquedas cortadas con -t se usa el tiempo ESTIMADO (intentos del
proceso duenio / tasa medida por proceso), y se marca con "(est.)".

Uso:  python3 scripts/resumen.py resultados/tiempos.csv
"""
import csv
import statistics
import sys
from collections import defaultdict


def segundos(fila):
    """Tiempo medido, o el estimado si la busqueda se corto por limite."""
    if fila["estimacion"]:
        return float(fila["estimacion"].split()[0]), True
    return float(fila["tiempo_s"]), False


def fmt(t):
    if t >= 365.25 * 86400:
        return f"{t:.3e} s (~{t / (365.25 * 86400):.1f} años)"
    if t >= 1:
        return f"{t:.3f} s"
    return f"{t * 1000:.3f} ms"


def num(x):
    return f"{x:.2e}" if x >= 1e4 else f"{x:,.2f}"


def main():
    filas = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
    grupos = defaultdict(list)
    for f in filas:
        grupos[(f["prueba"], f["programa"], f["estrategia"], int(f["procesos"]))].append(f)

    prom = {}
    print("## Promedios por prueba\n")
    print("| Prueba | Programa | Estr. | P | Reps | Resultado | Tiempo promedio | Desv. est. | Tasa por proceso (claves/s) |")
    print("|---|---|---|---|---|---|---|---|---|")
    for clave in sorted(grupos, key=lambda c: (c[0], c[1] != "secuencial", c[2], c[3])):
        g = grupos[clave]
        ts = [segundos(f) for f in g]
        estimado = any(e for _, e in ts)
        vals = [t for t, _ in ts]
        media = statistics.mean(vals)
        desv = statistics.stdev(vals) if len(vals) > 1 else 0.0
        tasa = statistics.mean(float(f["tasa_claves_s"]) for f in g) / clave[3]
        prom[clave] = (media, estimado)
        res = g[0]["resultado"].split("(")[0].strip()
        print(f"| {clave[0]} | {clave[1]} | {clave[2]} | {clave[3]} | {len(g)} | {res} | "
              f"{fmt(media)}{' (est.)' if estimado else ''} | {fmt(desv) if len(vals) > 1 else '—'} | {tasa:,.0f} |")

    print("\n## Speedup y eficiencia (S = T_secuencial / T_paralelo, E = S / p)\n")
    print("| Prueba | Estrategia | P | T secuencial | T paralelo | Speedup | Eficiencia |")
    print("|---|---|---|---|---|---|---|")
    for (prueba, prog, estr, p), (tp, est_p) in sorted(prom.items()):
        if prog != "mpi":
            continue
        sec = prom.get((prueba, "secuencial", "-", 1))
        if sec is None:
            continue
        ts, est_s = sec
        s = ts / tp if tp > 0 else float("inf")
        marca = " (est.)" if est_s or est_p else ""
        print(f"| {prueba} | {estr} | {p} | {fmt(ts)}{' (est.)' if est_s else ''} | "
              f"{fmt(tp)}{' (est.)' if est_p else ''} | {num(s)}{marca} | {num(s / p)} |")


if __name__ == "__main__":
    main()
