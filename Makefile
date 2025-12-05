# Compiler and flags
CC = gcc
CFLAGS = -I include

# Programs
PROGRAMS = game server window input dynamic obs_gen tar_gen

all: $(PROGRAMS)

game: main.c utils.c
	$(CC) $^ $(CFLAGS) -o $@

server: server.c utils.c
	$(CC) $^ $(CFLAGS) -o $@

window: window.c utils.c
	$(CC) $^ $(CFLAGS) -lncurses -o $@

input: input.c utils.c
	$(CC) $^ $(CFLAGS) -lncurses -o $@

dynamic: dynamic.c utils.c
	$(CC) $^ $(CFLAGS) -lm -o $@

obs_gen: obstacles_gen.c utils.c
	$(CC) $^ $(CFLAGS) -o $@

tar_gen: target_gen.c utils.c
	$(CC) $^ $(CFLAGS) -lm -o $@

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean