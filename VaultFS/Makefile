CC = g++
CFLAGS = -D_FILE_OFFSET_BITS=64 -O3
LIBS = -lfuse -ljsoncpp -lcurl

vaultfs:
	$(CC) -o $@ $(CFLAGS) $(LIBS) main.cpp
