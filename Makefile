CC = gcc
CFLAGS = -Wall -Wextra -std=c99

TARGET = lc3-vm
SRC = lc3.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean