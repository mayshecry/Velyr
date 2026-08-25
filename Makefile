CC ?= cc
PREFIX ?= /usr/local
CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter
ROOT := $(abspath .)
BIN := bin/velyr

.PHONY: all clean test examples install uninstall debug

all: $(BIN)

$(BIN): $(wildcard src/*.c) src/velyr.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -DVELYR_BUILD_ROOT='"$(ROOT)"' $(wildcard src/*.c) -o $@

examples: all
	$(BIN) build examples/hello.vy -o bin/hello
	$(BIN) build examples/fibonacci.vy -o bin/fibonacci
	$(BIN) build examples/parallel_sum.vy -o bin/parallel-sum
	$(BIN) build examples/calculator.vy -o bin/calculator

test: all
	./tests/run.sh

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer'

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 $(BIN) $(DESTDIR)$(PREFIX)/bin/velyr
	install -d $(DESTDIR)$(PREFIX)/lib/velyr/runtime
	install -m644 runtime/velyr_rt.h runtime/velyr_rt.c runtime/x86_64.S runtime/aarch64.S runtime/portable.c $(DESTDIR)$(PREFIX)/lib/velyr/runtime/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/velyr
	rm -rf $(DESTDIR)$(PREFIX)/lib/velyr

clean:
	rm -rf bin
	rm -f benchmark Test
	rm -f examples/hello examples/fibonacci examples/parallel_sum
