# Remaining tasks

## Improve Code Coverage

Target: 99+% line coverage for dsdl.c only

## Add nested composite serialization and deserialization tests

Add tests that verify serialization and deserialization of nested composites, including struct nesting struct, struct nesting union, union nestring struct, and union nesting union.

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

## Allow NULL value pointers in struct and union

See TODO comments in `dsdl.h`.

## Proper JSON serialization example

`examples/serialize_to_json.c` must include a recursive function that can serialize an arbitrary data type into JSON, and the name of the type is to be accepted via CLI args. The function will descend the tree and emit each item into JSON. The emission should be done via a callback accepting `wkv_str_t` instead of direct stdout access.
