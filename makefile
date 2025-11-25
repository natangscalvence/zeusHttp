CC = gcc
# Flags: Habilita warnings, padrão C11, símbolos de debug.
CFLAGS = -Wall -Wextra -std=c11 -g
# Linker: Necessário para funções de I/O de baixo nível (epoll).
LDFLAGS = -lrt

# -----------------------------------------------------------------------------
# Diretórios e Alvos
# -----------------------------------------------------------------------------
INCLUDE_DIR = include
SRC_DIR = src
CORE_DIR = src/core
HTTP_DIR = src/http

# Executável final
TARGET = zeushttp

# -----------------------------------------------------------------------------
# Arquivos Objeto (OBJS)
# -----------------------------------------------------------------------------
# Lista de todos os arquivos objeto. O '\' permite quebrar a linha.
OBJS = \
	$(CORE_DIR)/event_loop.o \
	$(HTTP_DIR)/http_parser.o \
	$(HTTP_DIR)/router.o \
	$(SRC_DIR)/main.o

# -----------------------------------------------------------------------------
# Regras de Compilação
# -----------------------------------------------------------------------------

# Regra principal: Cria o executável TARGET a partir dos objetos
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Event Loop: Depende da definição completa da conexão (conn.h)
$(CORE_DIR)/event_loop.o: $(CORE_DIR)/event_loop.c $(INCLUDE_DIR)/zeushttp.h $(INCLUDE_DIR)/http/http.h $(INCLUDE_DIR)/core/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

# Arquivo principal: Apenas a API pública
$(SRC_DIR)/main.o: $(SRC_DIR)/main.c $(INCLUDE_DIR)/zeushttp.h
	$(CC) $(CFLAGS) -c $< -o $@

# Router: Depende da definição completa da conexão (conn.h)
$(HTTP_DIR)/router.o: $(HTTP_DIR)/router.c $(INCLUDE_DIR)/zeushttp.h $(INCLUDE_DIR)/http/http.h $(INCLUDE_DIR)/core/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

# Parser HTTP: Depende da definição completa da conexão (conn.h)
$(HTTP_DIR)/http_parser.o: $(HTTP_DIR)/http_parser.c $(INCLUDE_DIR)/zeushttp.h $(INCLUDE_DIR)/http/http.h $(INCLUDE_DIR)/core/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------------------------------------------------------
# Regra de Limpeza
# -----------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Limpeza concluída."