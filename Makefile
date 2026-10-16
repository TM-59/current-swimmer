CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c

.PHONY: all test plots clean
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_fluid: $(SRC) tests/test_fluid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test_fluid
	./test_fluid

# Regenerates the CSV and the SVG charts shown in the README.
plots: current-swimmer
	mkdir -p docs
	./current-swimmer --headless docs

clean:
	rm -f current-swimmer test_fluid
