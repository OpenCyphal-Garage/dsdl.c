# Zubax C++ coding conventions

The reader is assumed to be familiar with:

- [MISRA C++](http://www.misra-cpp.com/)
- [High Integrity C++ Coding Standard](http://www.codingstandard.com/)
- [ROS C++ Style Guide](http://wiki.ros.org/CppStyleGuide)
- [C++ Core Guidelines](http://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)

For the recommended linter configuration (e.g., Clang-Format, Clang-Tidy), refer to one of the existing up-to-date projects, such as [cy](https://github.com/OpenCyphal-Garage/cy), [RAMEN](https://github.com/Zubax/ramen), et al.

## Naming

If the purpose of an entity can't be understood without a comment or without looking at the code, consider renaming it.

File names use `snake_case`; extensions for C++ are `.cpp`/`.hpp`.

Type names are `PascalCased`, including type template parameters and `using`/`typedef` declarations. Exception applies to pure C code, where it is allowed to use `snake_case` with the `_t` suffix.

Macros should be avoided entirely. In cases where it would be unpragmatic to do so, like in pure C code, they should use `SCREAMING_SNAKE_CASE`, as usual.

Everything else (variables, constants, funcitons, concepts, namespaces, non-type template parameters, etc) is `snake_case`. Private member variables may use the `m_` prefix or the `_` suffix.

A naming exception applies to math-heavy code, where the original math variable names take precendence over the naming recommendations given here. For example, to represent a variable named **Φ**, it is encouraged to use `Phi`, not `m_Phi`.

Global variables and module-local statics should be prefixed with `g_`, e.g. `g_foo`.

## Formatting

Blocks of code must be indented with 4 spaces. Usage of tabs anywhere in the source code is not permitted.

The following blocks should not be indented:

- Contents of a namespace;
- Case blocks in a switch statement.

Avoid declaring more than one variable (or field) on the same line.

Asterisk and ampersand characters must be placed with the type name, not with the variable name: `std::int32_t* ptr`.

Bit field declarations should put at least one space on the right of the colon: `unsigned field: 8`.

CV qualifiers must always be placed before the type name: `const FooBar& foobar`.

Very simple member functions (e.g. getters), trivial constructors, or trivial destructors can be defined on a single line.

Braces are mandatory for control statement blocks (see MISRA).

Class/union/struct members should be grouped by visibility in the following order: public, protected, private.

The maximum line length is exactly 120 characters.

Trailing whitespaces are not allowed; make sure to configure your text editor to automatically trim trailing whitespaces on save in the entire file. Every text file should contain exactly one blank line at the end.

Allowed end-of-line character sequence is Unix-style (`\n`, LF).

All comments should be single-line comments: `//` or `///` (for documentation comments). C-style multi-line comments should not be used. An exception applies to the case where a comment is inserted between valid tokens on the same line, such as `void foo(std::uint8_t* /* name omitted */)`. However, this pattern is discouraged.

## Coding

All variables must be default-initialized: `double foo = 0.0`. An uninitialized variable is to be considered a mistake, even if its value is to be immediately overwritten.

Variables that aren't supposed to change must be declared `const` or `constexpr`. Use `constexpr` where possible.

Variables must be defined *immediately before use*. Try to limit the scope of variables when possible (e.g., by scoping the code explicitly with braces).

It is explicitly disallowed to define all variables at the beginning of the function, the way often used with early versions of the C standard.

All member functions must be declared `const` whenever possible (see const correctness).

Functions that return values should be annotated with `[[nodiscard]]`, excepting overloaded operators.

Usage of the C-style cast in C++ code is strictly prohibited, with one exception of the cast to `void`. Use `static_cast`, `reinterpret_cast`, `const_cast`, and `dynamic_cast` instead.

Usage of `int`, `unsigned`, and all other native integral types is not permitted. Use `cstdint` instead.

Usage of `float`, `double`, `long double` is not recommended; consider aliasing them to `float32_t` and such instead.

Usage of the C library members from the global namespace is not allowed in C++ code; use the `std::` namespace instead. Cautionary tale: <https://github.com/PX4/PX4-Matrix/pull/41>.

The statement `using namespace` should not be used, except in a very local scope. Usage of this statement in a global scope or in a namespace scope is prohibited.

In C++ code, usage of the `NULL` macro or the literal zero (`0`) where a null pointer is expected is prohibited. Instead, use `nullptr`.

For conditional compilation, prefer `#if`, not `#ifdef`. Checking for the existence of the variable before use is encouraged.

In C++, all declarations except the `main()` function should reside within a namespace.

In C, all public entities should be prefixed with the name of the component they belong to (e.g. library name) in order to avoid name clashing.

Never use the keyword `class` in a template argument list, use `typename` instead. The special cases where the keyword `typename` was unacceptable have been removed in C++17.
