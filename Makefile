sockproc: sockproc.c
	$(CC) -Wall -Werror -o sockproc sockproc.c

test: sockproc
	./tests.sh 12345

clean:
	rm -f sockproc *.o
