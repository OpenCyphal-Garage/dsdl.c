# Cyphal DSDL parser in C

This is a compact C implementation of a Cyphal DSDL parser. Please read the Specification in the specs folder, peruse the reference implementations -- Nunavut (focus on C and Python language support only) and PyDSDL (the main reference implementation).

A crude draft of the API design is proposed in `dsdl.h`.

Directory `test_dsdl_root_namespaces` contains `0/` and `1/`, each with root namespaces (some split across both) for testing.

The core purpose is to allow very basic C applications, including some embedded ones, to load DSDL definitions at runtime, without relying on compile-time code generation. To simplify integration with constrained environments, the reliance on the C library is reduced to the bare minimum -- we do not use stdio or heap; the memory allocation facilities are provided by the user via a single realloc call.

The library will need to perform extensive name lookups; for that purpose it will leverage `lib/wkv.h`.

The entire implementation will be contained in a single C file named `dsdl.c`.

The implementation will be carried out in multiple steps roughly as follows (to be refined):

0. A test suite based on the ThrowTheSwitch Unity framework, or any other framework, needs to be set up, alongside a CMake-based build system. It is expected that some tests may need to access the internals of `dsdl.c`, which can be achieved by `#include <dsdl.c>`, similar to how it's done in libcanard/libudpard test suites. There must be at least one API-level test written in C++20, to make sure that the header is compatible with C++20+. All behaviors tested in the PyDSDL test suite must be tested for dsdl.c as well. Code coverage is needed.

1. A PEG parser needs to be implemented in C. It can be done from scratch, or using a simple third-party library or tool, whichever is easier, as long as no massive external dependencies are introduced. Considering that the grammar is very simple, it makes sense to make a simple ad-hoc parser directly in `dsdl.c`. Since this is a PEG grammar, the parser should be a PEG parser, bypassing the tokenizer. The test namespaces will be used to validate the parser.

2. Once the parser is done, serialization and deserialization logic needs to be implemented per the Specification. It will need to be cross-validated against Nunavut-generated C serialization code for every data type in the test namespaces, with randomly seeded field values.

The plan will need to be refined into finer-grained steps.

**See [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) for the detailed implementation plan.**

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

## Remaining tasks

### Address Alignment Cast Warnings in dsdl.c
**Impact**: Potential UB on strict-alignment platforms

clang-tidy reports multiple alignment warnings like:
```
Cast from 'char *' to 'wkv_str_t *' increases required alignment from 1 to 8
```

**Locations**: Lines 7688, 7692, 7696, 7698, 7700, 7823, 7825, 7827, 7829, 7831, 8558, 8715, 8731, 8747, 8786, 8819, 8916, 9583

**Analysis (2026-01-26)**:
- Pattern: `str_ptr` (char*) is used to carve out sections of a single allocation
- Memory source: `dsdl_alloc()` (user-provided realloc wrapper) - base is properly aligned
- Risk: After advancing str_ptr by struct sizes, alignment might be lost
- Reality: Works on all tested platforms (x86/x64) because struct sizes are multiples of alignment
- Technically UB per C standard, but safe in practice on common platforms

**To fix properly** (if needed for strict platforms):
1. Use `memcpy` to/from properly-aligned temporaries (verbose)
2. Add explicit alignment padding when advancing str_ptr
3. Use offsetof() calculations to ensure alignment
4. Estimated effort: 2-4 hours of careful refactoring

### Improve Code Coverage
**Target**: 99+% line coverage minimum, 100% recommended

**To improve later**:
```bash
cmake -S . -B build -DDSDL_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
cmake --build build --target coverage
# Check build/coverage/index.html
```

### Add Examples Directory
Add `examples/` with one or two very simple executables showing how to read DSDL files such that files from one namespace depends on files in another namespace, how to convert serialized data into JSON, possibly something else. Build examples with `DSDL_CONFIG_TRACE` enabled with logging to stderr, for demo purposes.

**Required**:
1. Create `examples/CMakeLists.txt`
2. Create `examples/load_multi_namespace.c` - Multi-namespace loading demo
3. Create `examples/serialize_to_json.c` - Serialization + JSON conversion demo
4. Enable `DSDL_CONFIG_TRACE` with stderr logging
5. Ensure README examples are up to date with new API (`dsdl_error_t* err` parameter)
