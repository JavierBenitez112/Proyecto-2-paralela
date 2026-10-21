## Promedios por prueba

| Prueba | Programa | Estr. | P | Reps | Resultado | Tiempo promedio | Desv. est. | Tasa por proceso (claves/s) |
|---|---|---|---|---|---|---|---|---|
| a | secuencial | - | 1 | 5 | CLAVE ENCONTRADA | 10.762 ms | 3.018 ms | 12,032,483 |
| a | mpi | 1 | 4 | 5 | CLAVE ENCONTRADA | 4.936 ms | 1.326 ms | 6,668,622 |
| a | mpi | 2 | 4 | 5 | CLAVE ENCONTRADA | 19.932 ms | 6.492 ms | 6,463,876 |
| b | secuencial | - | 1 | 3 | NO ENCONTRADA | 1.420e+09 s (~45.0 años) (est.) | 4.066e+07 s (~1.3 años) | 12,695,019 |
| b | mpi | 1 | 4 | 3 | NO ENCONTRADA | 7.302e+08 s (~23.1 años) (est.) | 13055015.330 s | 6,168,563 |
| b | mpi | 2 | 4 | 5 | CLAVE ENCONTRADA | 1.254 ms | 0.743 ms | 6,119,852 |
| c | secuencial | - | 1 | 3 | NO ENCONTRADA | 1.405e+09 s (~44.5 años) (est.) | 13871828.280 s | 12,824,448 |
| c | mpi | 1 | 4 | 3 | NO ENCONTRADA | 7.196e+08 s (~22.8 años) (est.) | 8860669.901 s | 6,259,392 |
| c | mpi | 2 | 4 | 5 | CLAVE ENCONTRADA | 0.757 ms | 0.200 ms | 6,365,417 |
| escala | secuencial | - | 1 | 5 | CLAVE ENCONTRADA | 5.330 s | 165.000 ms | 12,601,422 |
| escala | mpi | 1 | 1 | 5 | CLAVE ENCONTRADA | 5.070 s | 75.466 ms | 13,238,015 |
| escala | mpi | 1 | 2 | 5 | CLAVE ENCONTRADA | 2.589 s | 76.114 ms | 12,944,122 |
| escala | mpi | 1 | 4 | 5 | CLAVE ENCONTRADA | 2.625 s | 86.722 ms | 6,291,781 |
| escala | mpi | 2 | 1 | 5 | CLAVE ENCONTRADA | 5.044 s | 138.513 ms | 13,313,744 |
| escala | mpi | 2 | 2 | 5 | CLAVE ENCONTRADA | 2.558 s | 33.080 ms | 13,027,542 |
| escala | mpi | 2 | 4 | 5 | CLAVE ENCONTRADA | 2.648 s | 154.948 ms | 6,275,542 |
| motor-aesni | secuencial | - | 1 | 5 | CLAVE ENCONTRADA | 5.229 s | 181.860 ms | 12,846,680 |
| motor-evp | secuencial | - | 1 | 5 | CLAVE ENCONTRADA | 14.245 s | 476.259 ms | 4,715,473 |

## Speedup y eficiencia (S = T_secuencial / T_paralelo, E = S / p)

| Prueba | Estrategia | P | T secuencial | T paralelo | Speedup | Eficiencia |
|---|---|---|---|---|---|---|
| a | 1 | 4 | 10.762 ms | 4.936 ms | 2.18 | 0.55 |
| a | 2 | 4 | 10.762 ms | 19.932 ms | 0.54 | 0.13 |
| b | 1 | 4 | 1.420e+09 s (~45.0 años) (est.) | 7.302e+08 s (~23.1 años) (est.) | 1.94 (est.) | 0.49 |
| b | 2 | 4 | 1.420e+09 s (~45.0 años) (est.) | 1.254 ms | 1.13e+12 (est.) | 2.83e+11 |
| c | 1 | 4 | 1.405e+09 s (~44.5 años) (est.) | 7.196e+08 s (~22.8 años) (est.) | 1.95 (est.) | 0.49 |
| c | 2 | 4 | 1.405e+09 s (~44.5 años) (est.) | 0.757 ms | 1.86e+12 (est.) | 4.64e+11 |
| escala | 1 | 1 | 5.330 s | 5.070 s | 1.05 | 1.05 |
| escala | 1 | 2 | 5.330 s | 2.589 s | 2.06 | 1.03 |
| escala | 1 | 4 | 5.330 s | 2.625 s | 2.03 | 0.51 |
| escala | 2 | 1 | 5.330 s | 5.044 s | 1.06 | 1.06 |
| escala | 2 | 2 | 5.330 s | 2.558 s | 2.08 | 1.04 |
| escala | 2 | 4 | 5.330 s | 2.648 s | 2.01 | 0.50 |
