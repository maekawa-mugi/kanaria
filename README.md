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

The dump tool can regenerate split TSV files from a compiled dictionary:

```sh
./build/kanaria_dump_dictionary --split-dir dict
```

```cpp
#include "kanaria.h"

auto engine = kanaria::engine_create();
auto candidates = kanaria::engine_convert(*engine, "きょうはいいてんき");
```

`engine_convert` is the best-sentence convenience wrapper and returns at most
one candidate. The explicit OpenWnn-compatible workflow is:

```cpp
auto sentence = kanaria::engine_convert_best(*engine, "きょうはいいてんき");
auto clauses = kanaria::engine_get_clause_candidates(*engine,
                                                     "きょうはいいてんき", 0, 3);
auto resized = kanaria::engine_resize_clause(*engine,
                                             "きょうはいいてんき", 0, 3);
```

User and learned words remain inside the same frequency and connector model:

```cpp
kanaria::word entry{0, "カナリア", "かなりあ", 500, {190, 37}, 0};
kanaria::engine_add_user_word(*engine, entry);
kanaria::engine_learn_candidate(*engine, candidates.front());
```

Build:

```sh
cmake -S . -B build
cmake --build build
./build/kanaria_smoke
./build/kanaria_smoke きょうはいいてんき
./build/kanaria_smoke --romaji kyouha
./build/kanaria_benchmark
```

If `python3` is not visible to CMake, pass the interpreter explicitly:

```sh
cmake -S . -B build -DPYTHON3_EXECUTABLE=/path/to/python3
```

IBus frontend on Linux:

```sh
cmake -S . -B build -DKANARIA_BUILD_IBUS=ON
cmake --build build --target ibus-kanaria
cmake --install build
ibus restart
```

`ibus-kanaria` is built only when `pkg-config` can find the `ibus-1.0`
development package. The installed IBus component registers the `kanaria`
engine for Japanese input.

The API accepts and returns UTF-8 strings.
