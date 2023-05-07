.PHONY: all
all:
	gcc -o code main.c buddy.c -g -fsanitize=address -fsanitize=leak

run:
	./code

test: all run
