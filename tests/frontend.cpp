#include "kanaria_input_state.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

void type_ascii(kanaria_frontend::InputState& state, const std::string& text)
{
    for (unsigned char ch : text) {
        state.key_ascii(ch);
    }
}

bool has_candidate(const kanaria_frontend::InputState& state, const std::string& value)
{
    for (int i = 0; i < state.candidate_count(); ++i) {
        if (state.candidate(i) == value) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    bool ok = true;

    kanaria_frontend::InputState sentence_state;
    type_ascii(sentence_state, "watashinonamaehanakanodesu");
    ok &= expect(sentence_state.start_conversion(), "frontend conversion did not start");
    ok &= expect(sentence_state.candidate_count() > 0, "frontend conversion returned no candidates");
    const std::string first_clause_candidate = sentence_state.candidate(0);
    const std::string sentence_preedit = sentence_state.preedit();
    ok &= expect(first_clause_candidate != sentence_preedit,
                 "frontend mixed the sentence candidate into clause candidates");
    ok &= expect(sentence_state.commit() != first_clause_candidate,
                 "frontend committed a clause candidate instead of the full sentence");

    kanaria_frontend::InputState variant_state;
    type_ascii(variant_state, "kyou");
    ok &= expect(variant_state.start_conversion(), "variant conversion did not start");
    ok &= expect(has_candidate(variant_state, "キョウ"), "full-width katakana candidate was missing");
    ok &= expect(has_candidate(variant_state, "ｷｮｳ"), "half-width katakana candidate was missing");
    ok &= expect(has_candidate(variant_state, "ｋｙｏｕ"), "full-width alnum candidate was missing");
    ok &= expect(has_candidate(variant_state, "kyou"), "half-width alnum candidate was missing");

    return ok ? 0 : 1;
}
