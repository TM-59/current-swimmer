CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS  += -lm
# The terminal mode of src/main.c is POSIX; the feature macro is set here because a name starting
# with an underscore and a capital letter is reserved inside the program.
POSIX   := -D_POSIX_C_SOURCE=200809L
SRC     := src/fluid.c src/swimmer.c src/svg.c src/render.c src/gif.c src/wake.c
STORY   := src/fluid.c src/swimmer.c src/journey.c

.PHONY: all test bench story story-check web smoke play plots karman clean theory-figs theory-check
all: current-swimmer

current-swimmer: $(SRC) src/main.c
	$(CC) $(CFLAGS) $(POSIX) -o $@ $^ $(LDLIBS)

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

# ---- M61: local-only test standard (~/project-context/rules/test-standard.json) ----
# `make test` above stays the CI gate. `make check` runs every adopted kind through the
# common isolated runner (tm test) and writes git-ignored JUnit + test-results/summary.json.
TM_TEST_PYTHON ?= python3.14
TM_TEST_CMAKE ?= cmake
TM_TEST_CTEST ?= ctest
TM_TEST_NPM_CACHE ?= $(HOME)/.npm
TM_UNIT_BINS := test_fluid test_gif test_view test_story story_bench test_wake current-swimmer

.PHONY: test-unit check test-trace test-perf test-e2e tm-ctest tm-deps test-tm-unit test-tm-trace test-tm-perf test-tm-e2e
test-unit:
	$(TM_TEST_PYTHON) scripts/local-test.py unit

check:
	$(TM_TEST_PYTHON) scripts/local-test.py all

test-trace test-perf test-e2e:
	$(TM_TEST_PYTHON) scripts/local-test.py $(@:test-%=%)

tm-ctest:
	mkdir -p test-results
	"$(TM_TEST_CMAKE)" -S tests/ctest -B .tm-ctest >/dev/null

test-tm-unit test-tm-trace: tm-ctest
	$(MAKE) -j1 $(TM_UNIT_BINS)
	"$(TM_TEST_CTEST)" --test-dir .tm-ctest -j 1 -L '^$(@:test-tm-%=%)$$' --output-on-failure --output-junit "$(CURDIR)/test-results/$(@:test-tm-%=%).xml"

test-tm-perf: tm-ctest
	"$(TM_TEST_CTEST)" --test-dir .tm-ctest -j 1 -L '^perf$$' --output-on-failure --output-junit "$(CURDIR)/test-results/perf.xml"

tm-deps:
	@test -n "$(TM_TEST_RUN_ID)" || { echo "Requires isolated tm test" >&2; exit 1; }
	mkdir -p .tm-home && touch .tm-home/user.npmrc .tm-home/global.npmrc
	@test -d node_modules/playwright || npm ci --offline --ignore-scripts --no-audit --no-fund --userconfig="$(CURDIR)/.tm-home/user.npmrc" --globalconfig="$(CURDIR)/.tm-home/global.npmrc" --cache="$(TM_TEST_NPM_CACHE)"

test-tm-e2e: tm-ctest tm-deps
	"$(TM_TEST_CTEST)" --test-dir .tm-ctest -j 1 -L '^e2e$$' --output-on-failure --output-junit "$(CURDIR)/test-results/e2e.xml"

# ---- Formatter, linter and type checker (no ignores) ----
# C: clang-format, clang-tidy (.clang-tidy) and the compiler with -Wall -Wextra -Wpedantic -Werror.
# Python (scripts/*.py): Ruff format, Ruff with every rule, mypy strict. Shell: shfmt and ShellCheck.
# These are pinned by uv.lock. JavaScript, CSS, HTML, JSON: Biome (recommended rules, warnings fail)
# and tsc --checkJs strict, pinned by package-lock.json. TIDY_EXTRA passes -isystem paths when the C
# library headers are elsewhere.
RUN        = uv run --locked
STYLE_C    = $(shell git ls-files '*.c' '*.h')
STYLE_TU   = $(shell git ls-files '*.c')
STYLE_SH   = $(shell git ls-files '*.sh')
TIDY_EXTRA ?=
.PHONY: fmt lint typecheck check-style
fmt:
	$(RUN) clang-format -i $(STYLE_C)
	$(RUN) ruff format scripts
	$(RUN) ruff check --fix scripts
	$(RUN) shfmt -w -i 2 $(STYLE_SH)
	npm run fmt
lint:
	$(RUN) clang-format --dry-run --Werror $(STYLE_C)
	$(RUN) clang-tidy $(STYLE_TU) -- -std=c11 -Isrc -Itests $(POSIX) $(TIDY_EXTRA)
	$(RUN) ruff format --check scripts
	$(RUN) ruff check scripts
	$(RUN) shfmt -d -i 2 $(STYLE_SH)
	$(RUN) shellcheck $(STYLE_SH)
	npm run lint
typecheck:
	for f in $(STYLE_TU); do $(CC) $(CFLAGS) $(POSIX) -Isrc -Itests -fsyntax-only $$f || exit 1; done
	$(RUN) mypy
	npm run typecheck
check-style: lint typecheck
