#include "kanaria.h"

#include <iostream>
#include <algorithm>
#include <string>

namespace {

bool expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    auto engine = kanaria::engine_create();
    bool ok = true;

    const auto prediction2 = kanaria::engine_predict(*engine, "きょう", 2);
    ok &= expect(prediction2.size() == 2, "prediction limit 2 was not filled");

    const auto prediction5 = kanaria::engine_predict(*engine, "きょう", 5);
    ok &= expect(prediction5.size() == 5, "prediction limit 5 was not filled");

    const auto today = kanaria::engine_convert(*engine, "きょう", 20);
    ok &= expect(!today.empty() && today.front().candidate == "今日",
                 "dictionary candidate did not rank ahead of raw hiragana");
    ok &= expect(!today.empty()
                     && today.front().connection.left_id > 0
                     && today.front().connection.right_id > 0,
                 "candidate connection identifiers were not exposed");

    const std::string sentence = "きょうはいいてんき";
    const auto converted_sentence = kanaria::engine_convert(*engine, sentence, 20);
    ok &= expect(std::all_of(converted_sentence.begin(), converted_sentence.end(), [&](const auto& item) {
        return item.stroke == sentence;
    }), "clause fragments leaked into full-sentence candidates");

    const auto supplementary = kanaria::engine_convert(*engine, "😀", 5);
    ok &= expect(!supplementary.empty() && supplementary.back().candidate == "😀",
                 "supplementary Unicode character did not round-trip through UTF-16");

    std::string utf16_limit;
    for (int i = 0; i < 25; ++i) {
        utf16_limit += "😀";
    }
    ok &= expect(!kanaria::engine_convert(*engine, utf16_limit, 1).empty(),
                 "50-unit UTF-16 input was rejected");
    utf16_limit += "😀";
    ok &= expect(kanaria::engine_convert(*engine, utf16_limit, 1).empty(),
                 "UTF-16 input beyond the internal limit was accepted");

    return ok ? 0 : 1;
}
