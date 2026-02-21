BIN=shred
CFLAGS=-Wall -Wextra -Wpedantic
LDFLAGS=-lc

all: 
	$(CC) src/*.c $(CFLAGS) -c
	$(CC) -o $(BIN) *.o $(LDFLAGS)
