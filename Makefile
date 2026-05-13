CC = gcc
CFLAGS = -Wall -Wextra
SRC = src/main.c
TARGET = drone_fleet_os

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) src/*.o

.PHONY: all clean
