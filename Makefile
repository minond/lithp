CC = cc
CFLAGS = -std=c99 -Wall -ledit

build:
	$(CC) $(CFLAGS) prompt.c vendor/mpc/mpc.c -o lithp

clean:
	-rm lithp
