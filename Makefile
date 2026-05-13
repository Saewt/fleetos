CC = gcc
CFLAGS = -Wall -Wextra -g
SRC = $(wildcard src/*.c)
TARGET = drone_fleet_os

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) src/*.o

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
