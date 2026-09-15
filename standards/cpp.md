# C++ standards — Human History

Applies to everything under `src/`. C++17, MSVC (`cl` from the VS 2022 x64 tools), OpenMP.
General rules in `general.md` apply first; this file is what is specific to the language.

## Build

- Warnings at `/W3`, and a build introduces no new ones. `/O2 /openmp /EHsc /std:c++17`
  are the flags; a probe that needs different flags says why in its `build_*.bat`.
- Every executable has exactly one `build_*.bat` at the repo root, following `build.bat`.
  A probe without a build script does not exist.

## Numbers

- `float` for fields and per-cell state, `double` for geometry, integration and
  accumulation. This is the split the code already makes (`terrain::V3` against
  `geodesic::D3`); a function that crosses the line says which and why in a comment.
- Sums over many cells accumulate in `double`, even when the summands are `float`.
- Compare against a tolerance, never `==`, for anything that came out of arithmetic.
- Physical constants are `constexpr` with the unit in the name and a comment with the
  source or the reasoning where the value is not textbook.

## Hot paths

- Hot loops do not allocate. Buffers are sized once, at `World::build` or at the top of the
  probe, and reused; a `std::vector` constructed inside a per-cell or per-step loop is a bug.
- OpenMP over cells (`#pragma omp parallel for`) is the parallel idiom. No threads by hand
  in simulation code; the render thread in `main.cpp` is the one exception and stays there.
- No recursion in simulation paths. Iterate with an explicit stack or queue.
- Early returns over nesting. A condition that guards the whole body returns at the top.

## Shape

- A function reads in one screen. This is direction, not a hard count: a function already
  past it may not grow further without extracting first. Existing code is not rewritten to
  satisfy this rule.
- Parameters stay few. A function taking a cluster of related values takes the struct that
  holds them (`Climatology`, `hydrology::Result`, `SeasonCtx`), not the fields.
- `struct` with public fields for data. No getters and setters over plain data.
- `inline` functions in headers, since every executable is one translation unit. No
  separate `.cpp` for a module unless it is an executable's entry point.

## Types and ownership

- `std::vector` owns arrays; raw pointers and references only borrow, never own. No `new`
  outside the GL and Win32 plumbing in `main.cpp`.
- `const` by default; `const&` for anything larger than two words passed in.
- No `using namespace` in headers. Call across modules with the namespace spelled out
  (`atmosphere::seasonalTempC`), which is also how a reader finds the design note.
- `enum class` for closed sets; `switch` over it without a `default`, so a new member is a
  compiler warning at every use.

## Formatting

- `clang-format` with the repo's `.clang-format` is the formatter. It ships with the
  installed Visual Studio at
  `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.
- Format new files whole. In an existing file format only the lines you changed
  (`clang-format -i --lines=<from>:<to>`), so no commit reformats a file it did not
  otherwise touch and history stays readable.
- Style disagreements are settled in this file, not by tools.
