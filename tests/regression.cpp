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

    return ok ? 0 : 1;
}
