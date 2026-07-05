#include "kanaria_input_state.h"

#include <algorithm>
#include <cctype>

namespace kanaria_frontend {
namespace {

void append_candidate_unique(std::vector<kanaria::candidate>& candidates,
                             const kanaria::candidate& value)
{
    const auto same = std::find_if(candidates.begin(), candidates.end(), [&](const auto& known) {
        return known.candidate == value.candidate;
    });
    if (same == candidates.end()) {
        candidates.push_back(value);
    }
}

} // namespace

std::size_t utf8_codepoint_count(const std::string& s)
{
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xc0) != 0x80) {
            ++n;
        }
    }
    return n;
}

void pop_utf8_char(std::string& s)
{
    if (s.empty()) {
        return;
    }
    std::size_t pos = s.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80) {
        --pos;
    }
    s.erase(pos);
}

InputState::InputState() : engine_(kanaria::engine_create()) {}

bool InputState::ready() const { return engine_ != nullptr; }

bool InputState::key_ascii(unsigned int ch)
{
    if (!ready() || ch < 0x20 || ch > 0x7e) {
        return false;
    }
    roman_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    clear_conversion();
    return true;
}

bool InputState::backspace()
{
    if (converting_) {
        clear_conversion();
        return true;
    }
    if (!kana_utf8_.empty() && roman_.empty()) {
        pop_utf8_char(kana_utf8_);
        return true;
    }
    if (roman_.empty()) {
        return false;
    }
    roman_.pop_back();
    kana_utf8_.clear();
    clear_conversion();
    return true;
}

void InputState::cancel()
{
    roman_.clear();
    kana_utf8_.clear();
    clear_conversion();
}

bool InputState::start_conversion()
{
    clear_conversion();
    if (!make_kana()) {
        return false;
    }

    auto best = kanaria::engine_convert_best(*engine_, kana_utf8_);
    if (best) {
        append_candidate_unique(candidates_,
                                kanaria::candidate{best->value.candidate,
                                                   best->value.stroke,
                                                   best->value.frequency,
                                                   best->value.connection,
                                                   best->value.attribute});
    }

    const std::size_t len = utf8_codepoint_count(kana_utf8_);
    if (len > 0) {
        auto clause_candidates =
            kanaria::engine_get_clause_candidates(*engine_, kana_utf8_, 0, len, 64);
        for (const auto& value : clause_candidates) {
            append_candidate_unique(candidates_, value);
        }
    }
    if (candidates_.empty()) {
        candidates_.push_back(kanaria::candidate{kana_utf8_, kana_utf8_, 0, {}, 0});
    }
    candidate_index_ = 0;
    converting_ = true;
    return true;
}

bool InputState::next_candidate()
{
    if (candidates_.empty()) {
        return false;
    }
    candidate_index_ = (candidate_index_ + 1) % static_cast<int>(candidates_.size());
    converting_ = true;
    return true;
}

bool InputState::prev_candidate()
{
    if (candidates_.empty()) {
        return false;
    }
    candidate_index_ = (candidate_index_ + static_cast<int>(candidates_.size()) - 1)
        % static_cast<int>(candidates_.size());
    converting_ = true;
    return true;
}

bool InputState::select_candidate(int index)
{
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return false;
    }
    candidate_index_ = index;
    converting_ = true;
    return true;
}

std::string InputState::preedit()
{
    if (converting_ && !candidates_.empty()) {
        return candidates_[static_cast<std::size_t>(candidate_index_)].candidate;
    }
    if (!make_kana()) {
        return {};
    }
    return kana_utf8_;
}

std::string InputState::commit()
{
    std::string text;
    if (converting_ && !candidates_.empty()) {
        const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
        text = selected.candidate;
        kanaria::engine_learn_candidate(*engine_, selected);
    } else if (make_kana()) {
        text = kana_utf8_;
    }
    cancel();
    return text;
}

int InputState::candidate_count() const { return static_cast<int>(candidates_.size()); }
int InputState::candidate_index() const { return candidate_index_; }
bool InputState::is_converting() const { return converting_; }
bool InputState::has_text() const { return !roman_.empty() || !kana_utf8_.empty() || converting_; }

std::string InputState::candidate(int index) const
{
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return {};
    }
    return candidates_[static_cast<std::size_t>(index)].candidate;
}

void InputState::clear_conversion()
{
    candidates_.clear();
    candidate_index_ = 0;
    converting_ = false;
}

bool InputState::make_kana()
{
    if (!roman_.empty()) {
        kana_utf8_ = kanaria::romaji_to_hiragana(roman_);
    }
    return !kana_utf8_.empty();
}

} // namespace kanaria_frontend
