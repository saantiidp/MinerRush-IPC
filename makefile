#################################33
CC=gcc
CFLAGS= -g -Wall -pedantic -pthread
EXEC = miner monitor
########################
OBJECTS = pow.o
HEADS = cola.h bloque.h boolean.h
##########################

all: miner comprobador

miner: miner.o $(OBJECTS) 
	$(CC) $(CFLAGS) -o miner miner.o $(OBJECTS) -pthread -lrt 

comprobador: comprobador.o $(OBJECTS) 
	$(CC) $(CFLAGS) -o comprobador comprobador.o $(OBJECTS) -lrt 

miner.o: miner.c
	$(CC) $(CFLAGS) -c miner.c hilo.h $(HEADS)

comprobador.o: comprobador.c 
	$(CC) $(CFLAGS) -c comprobador.c $(HEADS)

pow.o: pow.c pow.h
	$(CC) -c pow.c pow.h


clear:
	rm -rf *.o

clean:
	rm -rf *.o $(EXEC)