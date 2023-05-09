all:
	gcc main.c coroutine.c -o a -g -pthread #-fsanitize=address -fsanitize=leak -fsanitize=undefined

run:
	./a

clear:
	rm a

test: all run