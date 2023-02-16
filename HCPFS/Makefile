CC = g++
CFLAGS = -D_FILE_OFFSET_BITS=64 -O3 -std=c++11
LIBS = -lfuse -ljsoncpp -lcurl

openapifs:
	$(CC) -o $@ $(CFLAGS) $(LIBS) main.cpp
