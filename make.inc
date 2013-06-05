CC=gcc -std=gnu9x
CXX=g++ -std=gnu++0x -D restrict=__restrict__
CPPFLAGS=-DNDEBUG -DUSE32BIT
CFLAGS=-fopenmp -g -O3 -march=native
LDLIBS=-lm -lrt -ldl
