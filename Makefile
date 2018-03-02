.phony all:
all: clean mts

mts: mts.c
	gcc mts.c -lpthread -lrt -lm -o mts

.PHONY clean:
clean:
	-rm -rf *.o *.exe