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

    kanaria_frontend::InputState space_state;
    ok &= expect(!space_state.key_ascii(' '), "frontend accepted space as composing text");
    ok &= expect(!space_state.has_text(), "frontend treated standalone space as composing text");
    ok &= expect(!space_state.start_conversion(), "frontend converted standalone space");

    kanaria_frontend::InputState kana_backspace_state;
    type_ascii(kana_backspace_state, "ariga");
    ok &= expect(kana_backspace_state.preedit() == kanaria::romaji_to_hiragana("ariga"),
                 "frontend did not compose ariga as kana");
    ok &= expect(kana_backspace_state.backspace(), "frontend did not backspace composed kana");
    ok &= expect(kana_backspace_state.preedit() == kanaria::romaji_to_hiragana("ari"),
                 "frontend backspace reverted to raw romaji instead of kana");

    kanaria_frontend::InputState yoon_backspace_state;
    type_ascii(yoon_backspace_state, "pikatyu");
    ok &= expect(yoon_backspace_state.preedit() == kanaria::romaji_to_hiragana("pikatyu"),
                 "frontend did not compose pikatyu as kana");
    ok &= expect(yoon_backspace_state.backspace(), "frontend did not backspace composed yoon kana");
    ok &= expect(yoon_backspace_state.preedit() == kanaria::romaji_to_hiragana("pikachi"),
                 "frontend yoon backspace did not leave the base kana");
    ok &= expect(yoon_backspace_state.backspace(), "frontend did not backspace yoon base kana");
    ok &= expect(yoon_backspace_state.preedit() == kanaria::romaji_to_hiragana("pika"),
                 "frontend yoon backspace left stale romaji behind");
    type_ascii(yoon_backspace_state, "tyuu");
    ok &= expect(yoon_backspace_state.preedit() == kanaria::romaji_to_hiragana("pikatyuu"),
                 "frontend did not recompose yoon kana after repeated backspace");
    ok &= expect(yoon_backspace_state.start_conversion(),
                 "frontend did not convert recomposed yoon kana");
    ok &= expect(has_candidate(yoon_backspace_state, "pikatyuu"),
                 "frontend lost the expected romaji candidate after yoon backspace");
    ok &= expect(!has_candidate(yoon_backspace_state, "pikattyuu"),
                 "frontend kept stale romaji after yoon backspace");
    ok &= expect(!has_candidate(yoon_backspace_state, "pikatttyuu"),
                 "frontend accumulated stale romaji after repeated yoon backspace");

    kanaria_frontend::InputState uppercase_yoon_backspace_state;
    type_ascii(uppercase_yoon_backspace_state, "PIKATYUU");
    ok &= expect(uppercase_yoon_backspace_state.backspace(),
                 "frontend did not backspace uppercase yoon trailing kana");
    ok &= expect(uppercase_yoon_backspace_state.backspace(),
                 "frontend did not backspace uppercase yoon small kana");
    ok &= expect(uppercase_yoon_backspace_state.preedit() == kanaria::romaji_to_hiragana("pikachi"),
                 "frontend uppercase yoon backspace did not leave the base kana");
    ok &= expect(uppercase_yoon_backspace_state.start_conversion(),
                 "frontend did not convert uppercase yoon backspace state");
    ok &= expect(has_candidate(uppercase_yoon_backspace_state, "PIKAち"),
                 "frontend did not preserve typed romaji case for a partially deleted yoon");
    ok &= expect(!has_candidate(uppercase_yoon_backspace_state, "pikachi"),
                 "frontend restored romaji from kana after uppercase yoon backspace");

    kanaria_frontend::InputState pending_roman_state;
    type_ascii(pending_roman_state, "na");
    ok &= expect(pending_roman_state.preedit() == kanaria::romaji_to_hiragana("na"),
                 "frontend did not preserve pending n for na composition");

    kanaria_frontend::InputState literal_ascii_state;
    type_ascii(literal_ascii_state, "a");
    ok &= expect(literal_ascii_state.key_ascii_literal('K'),
                 "frontend rejected literal shifted ASCII input");
    ok &= expect(literal_ascii_state.preedit() == kanaria::romaji_to_hiragana("a") + "K",
                 "frontend converted shifted ASCII through romaji table");
    ok &= expect(literal_ascii_state.start_conversion(),
                 "frontend did not convert literal shifted ASCII input");
    ok &= expect(has_candidate(literal_ascii_state, "aK"),
                 "frontend did not keep shifted ASCII available as a conversion candidate");

    kanaria_frontend::InputState tya_state;
    type_ascii(tya_state, "TYA-TYU-TYO");
    ok &= expect(tya_state.preedit() == kanaria::romaji_to_hiragana("tya-tyu-tyo"),
                 "frontend did not compose tya/tyu/tyo input");

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
        ok &= expect(version_state.candidate_page_count() >= 2,
                     "frontend did not expose a multi-page candidate count");
        ok &= expect(version_state.visible_candidate(0) != first_page_candidate,
                     "frontend candidate page did not refresh visible candidates");
        ok &= expect(version_state.prev_page(), "frontend did not move back to the previous page");
        ok &= expect(version_state.candidate_page_start() == 0,
                     "frontend candidate page did not return to the first page");
        ok &= expect(version_state.set_candidate_page(1), "frontend did not jump to a candidate page");
        ok &= expect(version_state.candidate_page_start() == 9,
                     "frontend candidate page jump did not select the requested page");
    }

    return ok ? 0 : 1;
}
