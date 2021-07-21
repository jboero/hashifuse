<<<<<<< HEAD
﻿CC = g++
CFLAGS = -D_FILE_OFFSET_BITS=64 -O3
LIBS = -Bstatic -lfuse -ljsoncpp -lcurl
=======
CC = g++
CFLAGS = $(CFLAGS) -D_FILE_OFFSET_BITS=64 -O3 -std=c++11
LIBS = -lfuse -ljsoncpp -lcurl
>>>>>>> f5ad2152e9c5409c9735ce4b0afe0364e1c25337

vaultfs:
	$(CC) -o $@ $(CFLAGS) $(LIBS) main.cpp
