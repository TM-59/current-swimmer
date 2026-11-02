CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c src/gif.c
STORY   := src/fluid.c src/swimmer.c src/journey.c

.PHONY: all test bench story story-check web smoke play plots clean
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_fluid: $(SRC) tests/test_fluid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_gif: src/gif.c tests/test_gif.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_view: src/fluid.c src/swimmer.c src/view.c tests/test_view.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_story: $(STORY) tests/test_story.c tests/story_run.h tests/story_policy.h src/journey.h
	$(CC) $(CFLAGS) -o $@ $(STORY) tests/test_story.c $(LDLIBS)

story_bench: $(STORY) tests/story_bench.c tests/story_run.h tests/story_policy.h src/journey.h
	$(CC) $(CFLAGS) -o $@ $(STORY) tests/story_bench.c $(LDLIBS)

# The story rows are compared byte for byte; generate and check them with the
# Debian 13 toolchain of the Dockerfile so libm rounding is the same everywhere.
test: test_fluid test_gif test_view test_story story_bench
	./test_fluid
	./test_gif
	./test_view
	./test_story
	./story_bench --check docs/narrative

bench: current-swimmer
	python3 scripts/bench.py

# Rewrites docs/narrative/{play-order,ablation,tension}.json (about 290 journeys).
story: story_bench
	mkdir -p docs/narrative
	./story_bench docs/narrative

# Regenerates every row into a scratch directory and compares the three files byte for byte.
story-check: story_bench
	rm -rf .story-check && mkdir .story-check
	./story_bench .story-check
	cmp .story-check/play-order.json docs/narrative/play-order.json
	cmp .story-check/ablation.json docs/narrative/ablation.json
	cmp .story-check/tension.json docs/narrative/tension.json
	rm -rf .story-check

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
	rm -f current-swimmer test_fluid test_gif test_view test_story story_bench
	rm -rf dist
