all: myhttpd.c
	cc -O myhttpd.c -o myhttpd -pthread

clean:
	$(RM) myhttpd
