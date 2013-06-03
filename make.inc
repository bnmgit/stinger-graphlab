CC=gcc -std=gnu9x
CXX=g++ -std=gnu++0x -D restrict=__restrict__
CFLAGS=-fopenmp -g -O2 -DUSE32BIT
LDLIBS=-lm -lrt -ldl
