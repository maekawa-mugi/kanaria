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
    const std::size_t first_clause_end = sentence_state.active_clause_end();
    ok &= expect(first_clause_candidate != sentence_preedit,
                 "frontend mixed the sentence candidate into clause candidates");
    ok &= expect(first_clause_end > 0 && first_clause_end < sentence_preedit.size(),
                 "frontend did not expose an active first-clause range");
    ok &= expect(sentence_state.next_clause(), "frontend did not move to the next clause");
    ok &= expect(sentence_state.active_clause_begin() == first_clause_end,
                 "frontend did not move the active-clause range to the next clause");
    ok &= expect(sentence_state.candidate(0) != first_clause_candidate,
                 "frontend did not refresh candidates for the next clause");
    ok &= expect(sentence_state.preedit() != sentence_state.candidate(0),
                 "frontend preedit stopped composing the whole sentence after clause movement");
    ok &= expect(sentence_state.commit() != first_clause_candidate,
                 "frontend committed a clause candidate instead of the full sentence");

    kanaria_frontend::InputState prediction_state;
    type_ascii(prediction_state, "kyou");
    ok &= expect(prediction_state.is_predicting(), "frontend did not expose predictions while composing");
    ok &= expect(prediction_state.has_candidate_window(), "frontend did not expose a prediction window");
    ok &= expect(!prediction_state.is_converting(), "frontend mixed prediction with conversion state");
    ok &= expect(prediction_state.candidate_count() > 0, "frontend prediction returned no candidates");
    const int prediction_index = prediction_state.candidate_index();
    ok &= expect(prediction_state.next_candidate(), "frontend prediction did not advance by candidate navigation");
    ok &= expect(prediction_state.candidate_index() != prediction_index,
                 "frontend prediction candidate index did not change");
    ok &= expect(prediction_state.start_conversion(), "frontend did not switch from prediction to conversion");
    ok &= expect(prediction_state.is_converting() && !prediction_state.is_predicting(),
                 "frontend did not leave prediction state after conversion started");

    kanaria_frontend::InputState variant_state;
    type_ascii(variant_state, "kyou");
    ok &= expect(variant_state.start_conversion(), "variant conversion did not start");
    ok &= expect(has_candidate(variant_state, "キョウ"), "full-width katakana candidate was missing");
    ok &= expect(has_candidate(variant_state, "ｷｮｳ"), "half-width katakana candidate was missing");
    ok &= expect(has_candidate(variant_state, "ｋｙｏｕ"), "full-width alnum candidate was missing");
    ok &= expect(has_candidate(variant_state, "kyou"), "half-width alnum candidate was missing");

    kanaria_frontend::InputState version_state;
    type_ascii(version_state, "ba-jonn");
    ok &= expect(version_state.start_conversion(), "version conversion did not start");
    ok &= expect(version_state.candidate(version_state.candidate_count() - 1) == "Kanaria-0.1",
                 "version candidate was not ordered last");
    ok &= expect(version_state.visible_candidate_count() <= 9,
                 "frontend exposed too many candidates on one page");
    if (version_state.candidate_count() > 9) {
        const std::string first_page_candidate = version_state.visible_candidate(0);
        ok &= expect(version_state.next_page(), "frontend did not move to the next candidate page");
        ok &= expect(version_state.candidate_page_start() == 9,
                     "frontend candidate page did not advance by one page");
        ok &= expect(version_state.visible_candidate(0) != first_page_candidate,
                     "frontend candidate page did not refresh visible candidates");
        ok &= expect(version_state.prev_page(), "frontend did not move back to the previous page");
        ok &= expect(version_state.candidate_page_start() == 0,
                     "frontend candidate page did not return to the first page");
    }

    return ok ? 0 : 1;
}
