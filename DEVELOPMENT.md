# Development Notes

## Coverage (GCC/Clang)

Build with coverage flags and run the test suite before generating reports:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDSDL_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target coverage
```

Notes:
- GCC uses gcov-compatible data directly.
- Clang also uses gcov-style coverage; `llvm-cov` is preferred if available.
- If coverage output looks stale, remove `build` and reconfigure.
- Coverage reports are generated in `build/coverage/index.html`

### Coverage Results

Current coverage (as of latest test run):
- **Lines**: 77.6% (5137 out of 6617)
- **Functions**: 98.4% (309 out of 314)
- **Branches**: 65.6% (3102 out of 4732)

**Caveats**:
- Some error paths are difficult to test without fault injection
- Parser error recovery paths may have lower coverage
- Platform-specific code paths (file I/O callbacks) are tested via mocks
- The 90%+ line coverage target is achievable with additional error path testing

## Tools

The following standalone tools live under `tools/` and use the public API:

```sh
./build/tools/dsdl_to_dsdl -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 validation.Expressions.0.1
./build/tools/dsdl_to_json -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 validation.Expressions.0.1
```

`dsdl_to_dsdl` emits normalized DSDL using constant types from the public API (see the header comment in its output).
