# M61: registers the existing checks as one CTest testcase each, so JUnit counts real programs.
# Labels select the kind: unit (what `make test` runs), trace (the [REQ-...] subset), perf, e2e.
# Generated JUnit: `make test-tm-<kind>` -> test-results/<kind>.xml. Run through `make check`.
cmake_minimum_required(VERSION 3.20)
project(current_swimmer_checks NONE)
enable_testing()
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_program(TM_MAKE make REQUIRED)
find_program(TM_PYTHON NAMES python3.14 python3 REQUIRED)

function(tm_check name labels)
  add_test(NAME "${name}" COMMAND ${ARGN})
  set_tests_properties("${name}" PROPERTIES WORKING_DIRECTORY "${ROOT}" LABELS "${labels}")
endfunction()

tm_check("test_fluid [REQ-pool-physics]" "unit;trace" ./test_fluid)
tm_check("test_gif" "unit" ./test_gif)
tm_check("test_view [REQ-pool-physics]" "unit;trace" ./test_view)
tm_check("test_story [REQ-story]" "unit;trace" ./test_story)
tm_check("test_wake [REQ-wake]" "unit;trace" ./test_wake)
tm_check("story_bench --check [REQ-story]" "unit;trace" ./story_bench --check docs/narrative)
tm_check("theory-check" "unit" ${TM_PYTHON} scripts/theory_check.py)
tm_check("bench" "perf" ${TM_MAKE} -j1 bench)
tm_check("smoke" "e2e" ${TM_MAKE} -j1 smoke)
