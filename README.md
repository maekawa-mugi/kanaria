# Kanaria

Kanaria is a Qt-free Japanese conversion engine with a standard C++23 API.

The original dictionary engine remains C. The new public API is intentionally
function-oriented and uses only standard C++ types such as `std::string`,
`std::vector`, and `std::unique_ptr`.

The bundled Japanese dictionary source lives in human-readable TSV files under
`dict/`. CMake packs those TSV files into a generated `KanariaDictionary.c` under the
build directory before compiling the library.

Human-readable dictionary dumps live in `dict/`, split by dictionary role:

- `dict/prediction.tsv` - 予測辞書
- `dict/independent_words.tsv` - 自立語辞書
- `dict/single_kanji.tsv` - 単漢字辞書
- `dict/suffix_words.tsv` - 接尾語辞書
- `dict/ancillary_words.tsv` - 付属語辞書
- `dict/uncompressed.tsv` - 未圧縮辞書
- other `dict/*.tsv` files - 自立語の追加辞書として取り込まれます。既存辞書へ追加したい場合は
  `independent_words_extra.tsv` や `ancillary_words_extra.tsv` のように既存辞書名を
  prefix にしてください。

The first `stem_offset` column is ignored while packing from TSV. It is kept only
for compatibility with dictionary dumps.

The dump tool can regenerate split TSV files from a compiled dictionary:

```sh
./build/kanaria_dump_dictionary --split-dir dict
```

```cpp
#include "kanaria.h"

auto engine = kanaria::engine_create();
auto candidates = kanaria::engine_convert(*engine, "きょうはいいてんき");
```

Build:

```sh
cmake -S . -B build
cmake --build build
./build/kanaria_smoke
./build/kanaria_smoke きょうはいいてんき
./build/kanaria_smoke --romaji kyouha
```

If `python3` is not visible to CMake, pass the interpreter explicitly:

```sh
cmake -S . -B build -DPYTHON3_EXECUTABLE=/path/to/python3
```

The API accepts and returns UTF-8 strings.
