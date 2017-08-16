CC = cc
CFLAGS = -std=c99 -W -Wall -ledit

build:
	$(CC) $(CFLAGS) prompt.c readline.c vendor/mpc/mpc.c -o lithp

clean:
	-rm lithp
