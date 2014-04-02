sockproc: sockproc.c
	gcc -Wall -Werror -o sockproc sockproc.c

test: sockproc
	./tests.sh 12345

clean:
	rm -f sockproc *.o
