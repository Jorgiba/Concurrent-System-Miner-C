########################################################

CC=gcc
CFLAGS= -g -pthread

########################################################

all: monitor miner

.PHONY: clean

########################################################
miner: miner.o pow.o
	$(CC) $(CFLAGS) -o miner miner.o pow.o -lrt

miner.o: miner.c
	$(CC) $(CFLAGS) -c miner.c

monitor: monitor.o
	$(CC) -o monitor monitor.o $(CFLAGS) -lrt

monitor.o:
	$(CC) -c monitor.c $(CFLAGS)

pow.o: pow.c
	$(CC) $(CFLAGS) -c pow.c

########################################################
clean:
	rm -f *.o monitor miner