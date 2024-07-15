srcFolder = ./src/
objFolder = ./build/
srcFiles = $(wildcard $(srcFolder)*.cpp)
objects = $(patsubst $(srcFolder)%.cpp, $(objFolder)%.o, $(srcFiles))
cflags = -Wall -g -lssl -lcrypto -O3 `pkg-config fuse --cflags --libs` -DDEBUG

all: CDCFS

$(objFolder)%.o: $(srcFolder)%.cpp
	@mkdir -p $(objFolder)
	$(CXX) $(cflags) -c -o $@ $<

CDCFS: $(objects)
	$(CXX) $(cflags) -o $@ $^

clean:
	rm -f CDCFS $(objFolder)*.o