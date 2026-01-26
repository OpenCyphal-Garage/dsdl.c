# Remaining tasks

## Improve Code Coverage

Target: 99+% line coverage for dsdl.c only

## Configure CI to fail if test coverage is too low

CI must require 99% coverage.

## Cleanup code

- Ensure the recommended header inclusion order is followed throughout: `dsdl.h` first, then the rest.
- Remove redundant comments, esp. TODO comments that are already addressed.
- Identify unused entities and eliminate them.
- Simplify what can be simplified.
- Eliminate redundancies.

## Identify and fix memory leaks

There probably are some memory leaks that need fixing.
