.PHONY: all
all:
	gcc -o test main.c buddy.c -g -fsanitize=address -fsanitize=leak

run:
	./test

test: all run
