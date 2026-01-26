# Remaining tasks

## dsdl_new() shall accept pointers to read() and list() callbacks

The header file is already updated but the implementation and the tools/examples/tests are not yet. Don't forget to update the README examples.

## Provide invocation examples for the examples in the readme

Something that the user could copy-paste to check out the examples.

## Identify and fix memory leaks

There probably are some memory leaks that need fixing.

## Cleanup code

- Ensure the recommended header inclusion order is followed throughout: `dsdl.h` first, then the rest.
- Identify unused entities and eliminate them.
- Simplify what can be simplified.
- Eliminate redundancies.

## Improve Code Coverage

Target: 99+% line coverage for dsdl.c only
