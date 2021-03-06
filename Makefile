CC = gcc
CPP = g++
CFLAGS = -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE
FUSE = -I/usr/include/fuse -lfuse
CURL = -lcurl
PREF = rsbt-
prefix =

.PHONY: clean install uninstall

all:
	@mkdir bin 2>/dev/null || true
	$(CPP) $(CFLAGS) -o bin/$(PREF)post src/post.cpp $(FUSE)
	$(CC) $(CFLAGS)  -o bin/$(PREF)hole src/hole.c $(FUSE)
	$(CC) $(CFLAGS)  -o bin/$(PREF)pre src/pre.c $(FUSE)
	$(CC) $(CFLAGS)  -o bin/$(PREF)crypt src/crypt.c $(FUSE)
	$(CC) $(CFLAGS)  -o bin/$(PREF)split src/split.c $(FUSE)
	$(CC) $(CFLAGS)  -o bin/$(PREF)http src/http.c $(FUSE) $(CURL)

install:
	install -m 0755 bin/$(PREF)post $(prefix)/bin
	install -m 0755 bin/$(PREF)hole $(prefix)/bin
	install -m 0755 bin/$(PREF)pre $(prefix)/bin
	install -m 0755 bin/$(PREF)crypt $(prefix)/bin
	install -m 0755 bin/$(PREF)split $(prefix)/bin
	install -m 0755 bin/$(PREF)http $(prefix)/bin

uninstall:
	rm -f $(prefix)/bin/$(PREF)post
	rm -f $(prefix)/bin/$(PREF)hole
	rm -f $(prefix)/bin/$(PREF)pre
	rm -f $(prefix)/bin/$(PREF)crypt
	rm -f $(prefix)/bin/$(PREF)split
	rm -f $(prefix)/bin/$(PREF)http

clean:
	rm -rf bin
