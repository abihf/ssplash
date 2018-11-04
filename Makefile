CC=gcc
CFLAGS=-march=native -mtune=native -O2 --param=ssp-buffer-size=4 -D_FORTIFY_SOURCE=2
LDFLAGS=-Wl,-O1,--sort-common,--as-needed,-z,relro
SOURCES=ssplash.c lodepng.c

build: dist/ssplash

install:
	install -o 0 -m 0755 dist/ssplash /usr/bin/ssplash

dist/ssplash: ssplash.c lodepng.c lodepng.h
	mkdir -p dist
	$(CC) $(CFLAGS) $(LDFLAGS) -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_ERROR_TEXT -o dist/ssplash $(SOURCES)

