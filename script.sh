gcc src/main.c src/utils.c -I include -o game
gcc src/server.c src/utils.c -I include -o server -lm
gcc src/window.c src/utils.c -I include  -lncurses -o window
gcc src/input.c src/utils.c -I include -lncurses -o input
gcc src/dynamic.c src/utils.c -I include -o dynamic -lm 
gcc src/obstacles_gen.c src/utils.c -I include  -o obs_gen
gcc src/target_gen.c src/utils.c -I include -o tar_gen -lm
gcc src/watchdog.c src/utils.c -I include -lncurses -o watchdog -lm

