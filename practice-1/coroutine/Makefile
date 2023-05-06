all:
	gcc main.c coroutine.c -o a -g -pthread -fsanitize=address

run:
	./a

clear:
	rm a

test: all run