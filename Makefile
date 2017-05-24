CC = cc
CFLAGS = -std=c99 -Wall -ledit

build:
	$(CC) $(CFLAGS) prompt.c -o prompt

clean:
	-rm prompt
