# Cyphal DSDL parser in C

This is a compact C implementation of a Cyphal DSDL parser. Please read the Specification in the specs folder, peruse the reference implementations -- Nunavut (focus on C and Python language support only) and PyDSDL (the main reference implementation).

A crude draft of the API design is proposed in `dsdl.h`.

Directory `test_dsdl_root_namespaces` contains various DSDL namespaces that can be used to test the parser against.

The core purpose is to allow very basic C applications, including some embedded ones, to load DSDL definitions at runtime, without relying on compile-time code generation. To simplify integration with constrained environments, the reliance on the C library is reduced to the bare minimum -- we do not use stdio or heap; the memory allocation facilities are provided by the user via a single realloc call.

The library will need to perform extensive name lookups; for that purpose it will leverage `lib/wkv.h`.

The entire implementation will be contained in a single C file named `dsdl.c`.

The implementation will be carried out in multiple steps roughly as follows (to be refined):

0. A test suite based on the ThrowTheSwitch Unity framework, or any other framework, needs to be set up, alongside a CMake-based build system. It is expected that some tests may need to access the internals of `dsdl.c`, which can be achieved by `#include <dsdl.c>`, similar to how it's done in libcanard/libudpard test suites. There must be at least one API-level test written in C++20, to make sure that the header is compatible with C++20+. All behaviors tested in the PyDSDL test suite must be tested for dsdl.c as well. Code coverage is needed.

1. A PEG parser needs to be implemented in C. It can be done from scratch, or using a simple third-party library or tool, whichever is easier, as long as no massive external dependencies are introduced. Considering that the grammar is very simple, it makes sense to make a simple ad-hoc parser directly in `dsdl.c`. Since this is a PEG grammar, the parser should be a PEG parser, bypassing the tokenizer. The test namespaces will be used to validate the parser.

2. Once the parser is done, serialization and deserialization logic needs to be implemented per the Specification. It will need to be cross-validated against Nunavut-generated C serialization code for every data type in the test namespaces, with randomly seeded field values.

The plan will need to be refined into finer-grained steps.

**See [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) for the detailed implementation plan.**

Feel free to add git submodules or install whatever software is needed to accomplish the task.

The project organiztion should roughly follow that of libcanard: https://github.com/OpenCyphal/libcanard/; with clang-tidy and clang-format set up.

## Language Requirements

- **C99+**: The library code must be compatible with C99 and later standards.
- **C++20+**: The public header (`dsdl.h`) must compile cleanly with C++20 and later.

## Style

Follow the Zubax Style Guide per `specs/CODING_CONVENTIONS.md`. Run Clang-Format regularly.

The code must be strictly C99-compliant, possibly with optional features enabled at compile time if a newer version of C is detected, and portable between all standard-compliant compilers (no compiler-specific features can be used; in particular, no `__attribute__` declarations are allowed).

**AGAIN:** COMPILER EXTENSIONS AND PLATFORM ASSUMPTIONS ARE NOT ALLOWED. Assume only standard C99.

The code must not make assumptions about the execution platform (pointer width, endianness, baremetal or not, etc.).

## Known issues (incomplete list)

- DSDL sizes (bit length, extent, etc) are tied to the native `size_t`, which is a mistake because it may cause overflow easily. We need to use `uint64_t` to represent sizes derived from DSDL definitions instead. This does not affect cases where the size is actually limited by the platform capabilities; e.g., `size_t extent` is a mistake because the extent can be arbitrarily large and it will easily cause overflow on a small platform with 16-bit size; OTOH, `size_t field_count` does not require changing because we wouldn't be able to store more than `SIZE_MAX` fields anyway. In the header file specifically, there is only two occurrence where the size needs updating: the extent case mentioned earlier and the array capacity.

- Do not assume that the target platform has `[u]uint8_t` and `[u]int16_t`; use `[u]int_least8_t` and `[u]int_least16_t` instead.

- Use a well-defined macro `DSDL_PATH_SEP` for path separator instead of literal `/`, and allow overriding it such that it defaults to `/` only if not defined.

- Use a well-defined macro `DSDL_PATH_MAX` instead of hardcoded adhoc `char dir_path[512]`, `char full_name[256];`, etc. Allow overriding it with 1024 being the default.

- Remove all other hardcoded size constants, if any.
