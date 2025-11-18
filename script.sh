gcc main.c utils.c 
gcc server.c utils.c -o server
gcc window.c utils.c  -lncurses -o window
gcc input.c utils.c -lncurses -o input
gcc dynamic.c utils.c -o dynamic
