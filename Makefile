CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c src/gif.c

.PHONY: all test plots clean
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_fluid: $(SRC) tests/test_fluid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_gif: src/gif.c tests/test_gif.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test_fluid test_gif
	./test_fluid
	./test_gif

# Regenerates the CSV and the SVG charts shown in the README.
plots: current-swimmer
	mkdir -p docs
	./current-swimmer --headless docs
	./current-swimmer --gif docs/demo.gif
	head -c6 docs/demo.gif | grep -q GIF89a

clean:
	rm -f current-swimmer test_fluid test_gif
