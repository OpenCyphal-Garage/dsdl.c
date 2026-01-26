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

- **Phase 0 Infrastructure:** CMake + Unity + C++20 API test are in place; x86/x64 matrix is green.
  Coverage target exists (gcovr/llvm-cov) but not verified.
- **Phase 1 PEG Parser:** Implemented and tested.
- **Phase 2 Semantic Analysis:** Implemented (type resolution, constants, assertions, extents, response types,
  fixed port ID). `name`/`name_versioned` and `constant_types` exposed in API.
  `dsdl_to_dsdl`/`dsdl_to_json` parity against PyDSDL is green (with fixed port-ID collision warning fallback).
- **Phase 3 Serialization/Deserialization:** Implemented with validation and error signaling.
  Nunavut cross-validation and truncation/saturation parity are still pending.
- **Phase 4 Testing/Validation:** Unit tests and PyDSDL parity test are green across x86/x64.
- **Phase 5 Polish/Docs:** Deferred.


## Next Steps

1. Verify GCC/Clang coverage output (gcovr/llvm-cov) and document caveats.
2. Nunavut cross-validation: generate Nunavut C code + auto-generate a C test that compares dsdl.c
   serialization byte-for-byte for deterministic randomized values.
3. Confirm truncation/saturation parity for serialization/deserialization.
4. Reduce PyDSDL comparator warnings by avoiding batch parse collisions (per-root or per-file for source roots).


## Phase 0: Project Infrastructure Setup

- Implemented: x86/x64, clang-tidy/format, sanitizers.
- Remaining: verify coverage output (gcovr/llvm-cov), add QEMU AVR target.
- Implemented: Unity tests + C++20 API test.
- Implemented: `dsdl_to_dsdl`, `dsdl_to_json`, PyDSDL comparator.
- Remaining: Nunavut codegen + cross-validation; CI (deferred).


## Phase 1: PEG Parser Implementation

- Implemented; see `dsdl.c` and unit tests.


## Phase 2: Semantic Analysis

- Implemented; parity tests are green.


## Phase 3: Serialization & Deserialization

- Implemented with validation and `SIZE_MAX` error signaling.
- Remaining: Nunavut cross-validation + truncation/saturation parity.


## Phase 4: Testing & Validation

- Current: unit tests + PyDSDL parity are green.
- Remaining: Nunavut cross-validation, verified coverage, QEMU AVR.


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
