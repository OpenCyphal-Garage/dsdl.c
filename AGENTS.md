# Cyphal DSDL parser in C

This is a compact C implementation of a Cyphal DSDL parser. Please read the Specification document in the specs folder, peruse the reference implementations -- Nunavut (focus on C and Python language support only) and PyDSDL (the main reference implementation).

A crude draft of the API design is proposed in `dsdl.h`.

Directory `test_dsdl_root_namespaces` contains various DSDL namespaces that can be used to test the parser against.

The core purpose is to allow very basic C applications, including some embedded ones, to load DSDL definitions at runtime, without relying on compile-time code generation. To simplify integration with constrained environments, the reliance on the C library is reduced to the bare minimum -- we do not use stdio or heap; the memory allocation facilities are provided by the user via a single realloc call.

The library will need to perform extensive name lookups; for that purpose it will leverage `lib/wkv.h`.

The entire implementation will be contained in a single C file named `dsdl.c`.

The implementation will be carried out in multiple steps roughly as follows (to be refined):

0. A test suite based on the ThrowTheSwitch Unity framework, or any other framework, needs to be set up, alongside a CMake-based build system.

1. A PEG parser needs to be implemented in C. It can be done from scratch, or using a simple third-party library or tool, whichever is easier, as long as no massive external dependencies are introduced. Considering that the grammar is very simple, it makes sense to make a simple ad-hoc parser directly in `dsdl.c`. The test namespaces will be used to validate the parser.

2. Once the parser is done, serialization and deserialization logic needs to be implemented per the Specification. It will need to be cross-validated against Nunavut-generated C serialization code for every data type in the test namespaces, with randomly seeded field values.

The plan will need to be refined into finer-grained steps.

Feel free to add git submodules or install whatever software is needed to accomplish the task.

The project organiztion should roughly follow that of libcanard: https://github.com/OpenCyphal/libcanard/; with clang-tidy and clang-format set up.
