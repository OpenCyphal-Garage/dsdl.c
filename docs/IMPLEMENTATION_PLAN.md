# Cyphal DSDL Parser in C: Implementation Plan

This document tracks the implementation plan for `dsdl.c`, a compact C library that parses Cyphal DSDL definitions
at runtime and provides serialization/deserialization.


## Design Constraints

- **Language:** C99+ (compatible with C99 and later standards)
- **C++ Compatibility:** Header must compile with C++20+
- **No stdio:** The library core does not depend on standard I/O
- **No heap:** Memory allocation only via user-provided `realloc` callback
- **Single file:** Entire implementation in `dsdl.c`
- **Minimal dependencies:** Only `lib/wkv.h` for name lookups
- **Rational precision:** Uses `intmax_t/uintmax_t` rationals; overflow is handled by halving operands


## Current Status

- **Phase 0 Infrastructure:** CMake + Unity + C++20 API test are in place; x86/x64 test matrix is green.
  Coverage target exists but needs verified GCC/Clang behavior and docs. No CI and no parity tooling yet.
- **Phase 1 PEG Parser:** Implemented and tested.
- **Phase 2 Semantic Analysis:** Implemented (type resolution, constants, assertions, extents, response types,
  fixed port ID). Needs full parity verification.
- **Phase 3 Serialization/Deserialization:** Implemented with basic validation and `SIZE_MAX` error signaling.
  Full cross-validation against Nunavut is missing; truncation/saturation parity needs confirmation.
- **Phase 4 Testing/Validation:** Unit tests exist; coverage reporting and cross-validation harnesses missing.
- **Phase 5 Polish/Docs:** Deferred.


## Next Steps

1. Coverage: verify and document coverage targets for GCC and Clang.
2. Parity tools: add standalone executables under `tools/`:
   - `dsdl_to_dsdl`: emit normalized DSDL with annotations using public API only.
   - `dsdl_to_json`: emit stable JSON using public API only.
3. PyDSDL comparator: Python script to compare outputs against PyDSDL for all valid/invalid namespaces.
4. Nunavut cross-validation: generate Nunavut C code + auto-generate a C test that compares dsdl.c
   serialization byte-for-byte for deterministic randomized values.
5. Fix mismatches and add targeted regression tests.


## Phase 0: Project Infrastructure Setup

### 0.1 Build System (CMake)

- CMake-based build with x86/x64 test matrix.
- clang-tidy/clang-format integration.
- Optional sanitizers.
- Coverage target (needs verification for GCC and Clang).

### 0.2 Test Framework Setup

- Unity test framework, internal tests via `#include "dsdl.c"`.
- C++20 API compatibility test.

### 0.3 Verification Infrastructure

- Standalone tools in `tools/` (not part of the library):
  - `dsdl_to_dsdl`: normalized DSDL + annotation comments.
  - `dsdl_to_json`: stable machine-readable JSON for parity checks.
- Python comparator against PyDSDL.
- Nunavut codegen + cross-validation test generation.
- CI (deferred).


## Phase 1: PEG Parser Implementation

- Hand-written PEG parser covering literals, expressions, types, directives, and services.
- Expression evaluation using rationals and closures.


## Phase 2: Semantic Analysis

- Type resolution with version selection.
- Constants, type attributes, and assertions (`@assert`).
- Extent handling and response type creation.
- Fixed port ID parsing.


## Phase 3: Serialization & Deserialization

- Bit buffer operations and primitive/array/composite serialization.
- Delimited composite handling and union tags.
- Validation for array lengths and union tags; error reporting via `SIZE_MAX`.
- Remaining work: cross-validate truncation/saturation, delimiter alignment, and boundary rules.


## Phase 4: Testing & Validation

- Current: unit tests for parser, resolution, BLS, serialization, and C++ API.
- Planned:
  - Full-namespace parse tests (all reference namespaces).
  - PyDSDL parity checks via `dsdl_to_*` tools.
  - Nunavut cross-validation for serialization/deserialization.
  - Verified coverage reporting.


## Phase 5: Polish & Documentation

- Public error codes (if needed) and API polish.
- Doxygen and usage examples.


## Success Criteria

1. All reference namespaces parse without error.
2. `dsdl_to_json` parity checks pass against PyDSDL for valid/invalid inputs.
3. Serialization matches Nunavut output byte-for-byte for all test types.
4. 90%+ code coverage with GCC/Clang where applicable.
5. C99 compliant library; header builds cleanly with C++20.
6. No stdio or heap dependencies in the library core.
