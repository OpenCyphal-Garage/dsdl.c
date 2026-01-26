# Instructions for agents

Please read the README, the Cyphal Specification (focus on the DSDL section), and peruse the reference implementations.

Directory `test_dsdl_root_namespaces` contains `0/` and `1/`, each with root namespaces (some split across both) for testing.

During development, please monitor CI status (e.g., using the `gh` app or whatever you prefer).

## Style

Follow the Zubax Style Guide per `specs/CODING_CONVENTIONS.md`. Use Clang-Format.

The header inclusion order is: own headers first (`dsdl.h`), then third-party libraries (if any), then standard library.

The code must be strictly C99-compliant, possibly with optional features enabled at compile time if a newer version of C is detected, and portable between all standard-compliant compilers (no compiler-specific features can be used; in particular, no `__attribute__` declarations are allowed).

The code must not make assumptions about the execution platform (pointer width, endianness, baremetal or not, etc.).

**AGAIN:** COMPILER EXTENSIONS AND PLATFORM ASSUMPTIONS ARE NOT ALLOWED. Assume only standard C99.
