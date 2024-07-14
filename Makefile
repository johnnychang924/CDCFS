CC = gcc
srcFolder = ./src/
objFolder = ./build/
srcFiles = $(wildcard $(srcFolder)*.c)
objects = $(patsubst $(srcFolder)%.c, $(objFolder)%.o, $(srcFiles))
cflags = -Wall -g -lssl -lcrypto -O3 `pkg-config fuse --cflags --libs`

all: CDCFS

$(objFolder)%.o: $(srcFolder)%.c
	@mkdir -p $(objFolder)
	$(CC) $(cflags) -c -o $@ $<

CDCFS: $(objects)
	$(CC) $(cflags) -o $@ $^

clean:
	rm -f CDCFS $(objFolder)*.o