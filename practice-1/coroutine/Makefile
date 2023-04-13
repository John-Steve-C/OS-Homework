all:
	gcc main.c coroutine.c -o a -g -fsanitize=address -ftrapv

run:
	./a

clear:
	rm a

test: all run