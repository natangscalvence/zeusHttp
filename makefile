CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LDFLAGS = -lrt

INCLUDE_DIR = include
SRC_DIR = src
CORE_DIR = src/core
HTTP_DIR = src/http
CORE_INCLUDE_DIR = $(INCLUDE_DIR)/core/
HTTP_INCLUDE_DIR = $(INCLUDE_DIR)/http/
HTTP_FILE_DIR = $(HTTP_DIR)
SECURITY_DIR = src/security




TARGET = zeushttp

OBJS = \
	$(CORE_DIR)/event_loop.o \
	$(HTTP_DIR)/http_parser.o \
	$(HTTP_DIR)/router.o \
	$(HTTP_DIR)/response.o \
	$(HTTP_FILE_DIR)/file.o \
	$(SECURITY_DIR)/privileges.o \
	$(SRC_DIR)/main.o

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(CORE_DIR)/event_loop.o: $(CORE_DIR)/event_loop.c $(INCLUDE_DIR)/zeushttp.h $(HTTP_INCLUDE_DIR)/http.h $(CORE_INCLUDE_DIR)/conn.h $(CORE_INCLUDE_DIR)/io_event.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/main.o: $(SRC_DIR)/main.c $(INCLUDE_DIR)/zeushttp.h
	$(CC) $(CFLAGS) -c $< -o $@

$(HTTP_DIR)/router.o: $(HTTP_DIR)/router.c $(INCLUDE_DIR)/zeushttp.h $(HTTP_INCLUDE_DIR)/http.h $(CORE_INCLUDE_DIR)/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

$(HTTP_DIR)/http_parser.o: $(HTTP_DIR)/http_parser.c $(INCLUDE_DIR)/zeushttp.h $(HTTP_INCLUDE_DIR)/http.h $(CORE_INCLUDE_DIR)/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

$(HTTP_DIR)/response.o: $(HTTP_DIR)/response.c $(INCLUDE_DIR)/zeushttp.h $(CORE_INCLUDE_DIR)/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

$(HTTP_FILE_DIR)/file.o: $(HTTP_FILE_DIR)/file.c $(INCLUDE_DIR)/zeushttp.h $(CORE_INCLUDE_DIR)/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SECURITY_DIR)/privileges.o: $(SECURITY_DIR)/privileges.c $(INCLUDE_DIR)/zeushttp.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Limpeza concluída."