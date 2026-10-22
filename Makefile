CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c src/gif.c

.PHONY: all test bench web smoke play plots clean
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_fluid: $(SRC) tests/test_fluid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_gif: src/gif.c tests/test_gif.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_view: src/fluid.c src/swimmer.c src/view.c tests/test_view.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test_fluid test_gif test_view
	./test_fluid
	./test_gif
	./test_view

bench: current-swimmer
	python3 scripts/bench.py

web:
	bash scripts/build-web.sh

smoke: web
	node scripts/smoke.mjs

play: web
	python3 -m http.server 4204 --bind 127.0.0.1 --directory dist

# Regenerates the CSV and the SVG charts shown in the README.
plots: current-swimmer
	mkdir -p docs
	./current-swimmer --headless docs
	./current-swimmer --gif docs/demo.gif
	head -c6 docs/demo.gif | grep -q GIF89a

clean:
	rm -f current-swimmer test_fluid test_gif test_view
	rm -rf dist
