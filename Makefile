CC       = gcc
MPICC    = mpicc
CFLAGS   = -O2 -Wall -Wno-deprecated-declarations
LDLIBS   = -lssl -lcrypto

SRC_DIR  = src
BIN_DIR  = bin

.PHONY: all clean

all: $(BIN_DIR)/secuencial $(BIN_DIR)/paralelo_naive $(BIN_DIR)/paralelo_intercalado

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/secuencial: $(SRC_DIR)/secuencial.c $(SRC_DIR)/des_lib.c $(SRC_DIR)/util.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/paralelo_naive: $(SRC_DIR)/paralelo_naive.c $(SRC_DIR)/des_lib.c $(SRC_DIR)/util.c | $(BIN_DIR)
	$(MPICC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/paralelo_intercalado: $(SRC_DIR)/paralelo_intercalado.c $(SRC_DIR)/des_lib.c $(SRC_DIR)/util.c | $(BIN_DIR)
	$(MPICC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(BIN_DIR)
