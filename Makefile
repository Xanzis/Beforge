CC = gcc
beforge : beforge.c
#		$(CC) beforge.c -g -fsanitize=address -o beforge -Wall
		$(CC) beforge.c -o beforge