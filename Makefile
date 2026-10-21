# CC3069 - Computacion Paralela y Distribuida - Proyecto 02
#
#   make              compila todo en bin/
#   make lab03        solo los programas del Laboratorio 03
#   make proyecto02   solo los programas MPI del Proyecto 02 (src/proyecto02/*.c)
#   make run-original / run-mejorado ARGS="-k 12345"
#   make run-mpi PROG=<nombre> NP=4 ARGS="..."
#   make clean | help

# ---------------------------------------------------------------- herramientas
CC       ?= cc
MPICC    ?= mpicc
MPIRUN   ?= mpirun
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS += -Iinclude
PREFIX   ?= /usr/local

OPENSSL_CFLAGS := $(shell pkg-config --cflags libcrypto 2>/dev/null)
OPENSSL_LIBS   := $(shell pkg-config --libs libcrypto 2>/dev/null || echo -lcrypto)
CPPFLAGS += $(OPENSSL_CFLAGS)
LDLIBS   += $(OPENSSL_LIBS)

# ---------------------------------------------------------------- directorios
SRC_DIR   := src
BUILD_DIR := build
BIN_DIR   := bin

# Rutinas compartidas (secuencial y MPI)
COMMON_SRC := $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJ := $(COMMON_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
COMMON_HDR := $(wildcard include/*.h)

# Laboratorio 03
LAB03_ORIGINAL := $(BIN_DIR)/busqueda_clave_aes_secuencial
LAB03_MEJORADO := $(BIN_DIR)/busqueda_clave_aes_secuencial_mejorado
LAB03_BINS     := $(LAB03_ORIGINAL) $(LAB03_MEJORADO)

# Proyecto 02: cada src/proyecto02/<nombre>.c genera bin/<nombre> con mpicc
P02_SRC  := $(wildcard $(SRC_DIR)/proyecto02/*.c)
P02_BINS := $(P02_SRC:$(SRC_DIR)/proyecto02/%.c=$(BIN_DIR)/%)

# ---------------------------------------------------------------- objetivos
.SECONDARY:
.PHONY: all lab03 proyecto02 run-original run-mejorado run-mpi install clean help

all: lab03 proyecto02

lab03: $(LAB03_BINS)

proyecto02: $(P02_BINS)

# Programa original del enunciado: autocontenido, no usa src/common.
$(LAB03_ORIGINAL): $(SRC_DIR)/lab03/busqueda_clave_aes_secuencial.c | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(LAB03_MEJORADO): $(BUILD_DIR)/lab03/busqueda_clave_aes_secuencial_mejorado.o $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/%: $(BUILD_DIR)/proyecto02/%.o $(COMMON_OBJ) | $(BIN_DIR)
	$(MPICC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/proyecto02/%.o: $(SRC_DIR)/proyecto02/%.c $(COMMON_HDR)
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

# ---------------------------------------------------------------- utilidades
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(LAB03_BINS) $(P02_BINS) $(DESTDIR)$(PREFIX)/bin

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

help:
	@sed -n '3,9p' Makefile | sed 's/^# \{0,1\}//'
