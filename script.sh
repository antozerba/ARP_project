gcc main.c utils.c -I include 
gcc server.c utils.c -I include -o server
gcc window.c utils.c -I include  -lncurses -o window
gcc input.c utils.c -I include -lncurses -o input
gcc dynamic.c utils.c -I include -o dynamic -lm 
gcc obstacles_gen.c utils.c -I include  -o obs_gen
gcc target_gen.c utils.c -I include -o tar_gen -lm

