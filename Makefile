.phony all:
all: clean Main

Main: Main.c
	gcc Main.c -lpthread -lrt -o Main

.PHONY clean:
clean:
	-rm -rf *.o *.exe