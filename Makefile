sockproc: sockproc.c
	gcc -Wall -Werror -o sockproc sockproc.c

clean:
	rm -f sockproc *.o
