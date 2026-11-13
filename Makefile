CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c src/gif.c src/wake.c
STORY   := src/fluid.c src/swimmer.c src/journey.c

.PHONY: all test bench story story-check web smoke play plots karman clean theory-figs theory-check
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_fluid: $(SRC) tests/test_fluid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_gif: src/gif.c tests/test_gif.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_view: src/fluid.c src/swimmer.c src/view.c tests/test_view.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_wake: src/wake.c tests/test_wake.c src/wake.h
	$(CC) $(CFLAGS) -o $@ src/wake.c tests/test_wake.c $(LDLIBS)

test_story: $(STORY) tests/test_story.c tests/story_run.h tests/story_policy.h src/journey.h
	$(CC) $(CFLAGS) -o $@ $(STORY) tests/test_story.c $(LDLIBS)

story_bench: $(STORY) tests/story_bench.c tests/story_run.h tests/story_policy.h src/journey.h
	$(CC) $(CFLAGS) -o $@ $(STORY) tests/story_bench.c $(LDLIBS)

# The story rows are compared byte for byte; generate and check them with the
# Debian 13 toolchain of the Dockerfile so libm rounding is the same everywhere.
test: test_fluid test_gif test_view test_story story_bench test_wake
	./test_fluid
	./test_gif
	./test_view
	./test_story
	./test_wake
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

# M2: the cylinder wake at Re 150 (about 20 s): docs/karman.svg and docs/karman-forces.svg.
karman: current-swimmer
	./current-swimmer --karman docs

# Regenerates the plotted figures and the measured-values table of docs/theory (one command).
theory_figs: $(STORY) src/view.c src/svg.c src/render.c src/wake.c tests/theory_figs.c tests/story_run.h tests/story_policy.h src/journey.h src/view.h src/svg.h src/render.h src/wake.h
	$(CC) $(CFLAGS) -o $@ $(STORY) src/view.c src/svg.c src/render.c src/wake.c tests/theory_figs.c $(LDLIBS)

theory-figs: theory_figs
	mkdir -p docs/theory/figures
	./theory_figs docs/theory/figures

# Fails when a statement of docs/theory has no figure, a link is broken, or a referenced function or test is gone.
theory-check:
	python3 scripts/theory-check.py

clean:
	rm -f current-swimmer test_fluid test_gif test_view test_story story_bench theory_figs test_wake
	rm -rf dist
