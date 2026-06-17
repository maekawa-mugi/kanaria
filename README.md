# OpenWnn std C++23 Port

This is a Qt-free C++23 porting layer for Qt's bundled OpenWnn source.

The original dictionary engine remains C. The new public API is intentionally
function-oriented and uses only standard C++ types such as `std::string`,
`std::vector`, and `std::unique_ptr`.

```cpp
#include "openwnn_std.h"

auto engine = openwnn::engine_create();
auto candidates = openwnn::engine_convert(*engine, "きょうはいいてんき");
```

Build:

```sh
cmake -S . -B build
cmake --build build
./build/openwnn_smoke
./build/openwnn_smoke きょうはいいてんき
./build/openwnn_smoke --romaji kyouha
```

If CMake is not installed, compile the C files as C objects, then link them
with `src/openwnn_std.cpp` and your application using a C++23 compiler.

The API accepts and returns UTF-8 strings.
