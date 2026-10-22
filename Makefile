# CC3069 - Computacion Paralela y Distribuida - Proyecto 02
#
#   make              compila todo en bin/
#   make lab03        solo los programas del Laboratorio 03
#   make proyecto02   solo los programas MPI del Proyecto 02 (src/proyecto02/*.c)
#   make run-original / run-mejorado ARGS="-k 12345"
#   make run-mpi PROG=<nombre> NP=4 ARGS="..."
#   make test         pruebas automaticas (scripts/pruebas.sh)
#   make demo         pruebas a) b) c) con -np 4 y ambas estrategias (LIMITE=30 s)
#   make bitacora     mediciones del Anexo 2 -> resultados/
#   make informe      regenera el informe (docx y PDF) desde resultados/
#   make clean | help

# ---------------------------------------------------------------- herramientas
CC       ?= cc
MPICC    ?= mpicc
MPIRUN   ?= mpirun
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS += -Iinclude
PREFIX   ?= /usr/local

# pkg-config si existe; si no, el OpenSSL de Homebrew (macOS) o el del sistema.
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
OPENSSL_CFLAGS := $(shell pkg-config --cflags libcrypto 2>/dev/null || \
                    { [ -n "$(OPENSSL_PREFIX)" ] && echo -I$(OPENSSL_PREFIX)/include; })
OPENSSL_LIBS   := $(shell pkg-config --libs libcrypto 2>/dev/null || \
                    { [ -n "$(OPENSSL_PREFIX)" ] && echo -L$(OPENSSL_PREFIX)/lib; }; echo -lcrypto)
CPPFLAGS += $(OPENSSL_CFLAGS)
LDLIBS   += $(OPENSSL_LIBS)

# ---------------------------------------------------------------- directorios
SRC_DIR   := src
BUILD_DIR := build
BIN_DIR   := bin

# Rutinas compartidas: cada grupo de programas enlaza solo las suyas
LAB03_OBJ  := $(BUILD_DIR)/common/aes_busqueda.o
P02_OBJ    := $(BUILD_DIR)/common/aes_texto.o
COMMON_HDR := $(wildcard include/*.h)

# Laboratorio 03
LAB03_ORIGINAL := $(BIN_DIR)/busqueda_clave_aes_secuencial
LAB03_MEJORADO := $(BIN_DIR)/busqueda_clave_aes_secuencial_mejorado
LAB03_BINS     := $(LAB03_ORIGINAL) $(LAB03_MEJORADO)

# Proyecto 02: cada src/proyecto02/<nombre>.c genera bin/<nombre>;
# solo busqueda_mpi usa MPI y se compila con mpicc
P02_SRC  := $(wildcard $(SRC_DIR)/proyecto02/*.c)
P02_BINS := $(P02_SRC:$(SRC_DIR)/proyecto02/%.c=$(BIN_DIR)/%)
P02_MPI  := $(BIN_DIR)/busqueda_mpi

# ---------------------------------------------------------------- objetivos
.SECONDARY:
.PHONY: all lab03 proyecto02 run-original run-mejorado run-mpi test demo bitacora informe install clean help

all: lab03 proyecto02

lab03: $(LAB03_BINS)

proyecto02: $(P02_BINS)

# Programa original del enunciado: autocontenido, no usa src/common.
$(LAB03_ORIGINAL): $(SRC_DIR)/lab03/busqueda_clave_aes_secuencial.c | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(LAB03_MEJORADO): $(BUILD_DIR)/lab03/busqueda_clave_aes_secuencial_mejorado.o $(LAB03_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(P02_MPI): $(BUILD_DIR)/proyecto02/busqueda_mpi.o $(P02_OBJ) | $(BIN_DIR)
	$(MPICC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/%: $(BUILD_DIR)/proyecto02/%.o $(P02_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/proyecto02/busqueda_mpi.o: $(SRC_DIR)/proyecto02/busqueda_mpi.c $(COMMON_HDR)
	@mkdir -p $(@D)
	$(MPICC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(COMMON_HDR)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $@

# ---------------------------------------------------------------- ejecucion
NP ?= 4

run-original: $(LAB03_ORIGINAL)
	./$<

run-mejorado: $(LAB03_MEJORADO)
	./$< $(ARGS)

run-mpi: $(BIN_DIR)/$(PROG)
	$(MPIRUN) -np $(NP) ./$< $(ARGS)

# ---------------------------------------------------------------- pruebas y bitacora
LIMITE ?= 30
DEMO_FLAGS ?= --oversubscribe

test: all
	bash scripts/pruebas.sh

demo: all
	@for k in 123456 18014398509481984 18014398509481985; do \
	    for e in 1 2; do \
	        echo "\$$ $(MPIRUN) -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k $$k -e $$e -t $(LIMITE)"; \
	        $(MPIRUN) $(DEMO_FLAGS) -np 4 ./bin/busqueda_mpi -i datos/prueba.txt -k $$k -e $$e -t $(LIMITE) || exit 1; \
	        echo; \
	    done; \
	done

bitacora: all
	bash scripts/bitacora.sh

informe:
	python3 scripts/generar_informe.py

# ---------------------------------------------------------------- utilidades
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(LAB03_BINS) $(P02_BINS) $(DESTDIR)$(PREFIX)/bin

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

help:
	@sed -n '3,13p' Makefile | sed 's/^# \{0,1\}//'
