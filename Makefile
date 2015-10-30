CC=gcc
CFLAGS=-I.

webserver: webserver.c
	$(CC) -o webserver webserver.c $(CFLAGS).
