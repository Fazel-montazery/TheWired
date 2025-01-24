src := wired.c
target := wired.out
libs := -Ls -lformw -lncursesw
cc := gcc
flags := -Wall -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 -static

all: main

main: ${src}
	${cc} -o ${target} ${src} ${libs} ${flags} && strip ${target}

server: server.c
	gcc -o server.out server.c -O2 -Wall

serverdbg: server.c
	gcc -o server.out server.c -g3 -fsanitize=address -Wall

debug: ${src}
	${cc} -o ${target} ${src} ${libs} -g3 -fsanitize=address -Wall

check:
	valgrind --leak-check=yes ./${target}
