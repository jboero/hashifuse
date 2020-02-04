CC = g++
CFLAGS = -D_FILE_OFFSET_BITS=64 -O3 -std=c++11
LIBS = -lfuse -ljsoncpp -lcurl

vaultfs:
	$(CC) -g -o $@ $(CFLAGS) $(LIBS) main.cpp
