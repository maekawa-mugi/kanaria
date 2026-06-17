# OpenWnn std C++23 Port

This is a Qt-free C++23 porting layer for Qt's bundled OpenWnn source.

The original dictionary engine remains C. The new public API is intentionally
function-oriented and uses only standard C++ types such as `std::string`,
`std::vector`, and `std::unique_ptr`.

The bundled Japanese dictionary source remains in OpenWnn's original binary C
array format at `src/wnnEngine/WnnJpnDic.c`. CMake extracts that into generated
intermediate files under the build directory before compiling the library.

Human-readable dictionary dumps live in `dict/`, split by dictionary role:

- `dict/prediction.tsv` - 予測辞書
- `dict/independent_words.tsv` - 自立語辞書
- `dict/single_kanji.tsv` - 単漢字辞書
- `dict/no_reading.tsv` - 読みなし辞書
- `dict/ancillary_words.tsv` - 付属語辞書
- `dict/uncompressed.tsv` - 未圧縮辞書

The dump tool can regenerate the split TSV files:

```sh
./build/openwnn_dump_dictionary --split-dir dict
```

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

If `python3` is not visible to CMake, pass the interpreter explicitly:

```sh
cmake -S . -B build -DPYTHON3_EXECUTABLE=/path/to/python3
```

The API accepts and returns UTF-8 strings.
