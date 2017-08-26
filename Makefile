CC = cc
CFLAGS = -std=c11 -W -Wall -ledit

build:
	$(CC) $(CFLAGS) lithp.c readline.c vendor/mpc/mpc.c -o lithp

clean:
	-rm lithp
