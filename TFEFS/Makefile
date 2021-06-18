CC = g++
CFLAGS = -D_FILE_OFFSET_BITS=64 -O3 -std=c++11 -static
LIBS = -lfuse -ljsoncpp -lcurl

tfefs:
	$(CC) -o $@ $(CFLAGS) $(LIBS) main.cpp
