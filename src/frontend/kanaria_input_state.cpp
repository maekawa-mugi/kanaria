#include "kanaria_input_state.h"

#include <algorithm>
#include <cctype>

namespace kanaria_frontend {
namespace {

constexpr int candidate_page_size = 9;

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

bool is_doubled_consonant(const std::string& text)
{
    if (text.size() < 2 || text[0] != text[1] || text[0] == 'n') {
        return false;
    }
    return std::string("bcdfghjklmpqrstvwxyz").find(text[0]) != std::string::npos;
}

const std::vector<std::string>& romaji_keys()
{
    static const std::vector<std::string> keys = {
        "zh", "zj", "zk", "zl",
        "a", "i", "u", "e", "o",
        "ka", "ki", "ku", "ke", "ko",
        "sa", "shi", "si", "su", "se", "so",
        "ta", "chi", "ti", "tsu", "tu", "te", "to",
        "na", "ni", "nu", "ne", "no",
        "ha", "hi", "fu", "hu", "he", "ho",
        "ma", "mi", "mu", "me", "mo",
        "ya", "yu", "yo",
        "ra", "ri", "ru", "re", "ro",
        "wa", "wo", "nn", "n",
        "ga", "gi", "gu", "ge", "go",
        "za", "ji", "zi", "zu", "ze", "zo",
        "da", "di", "du", "de", "do",
        "ba", "bi", "bu", "be", "bo",
        "pa", "pi", "pu", "pe", "po",
        "kya", "kyu", "kyo",
        "sha", "shu", "sho",
        "cha", "chu", "cho",
        "nya", "nyu", "nyo",
        "hya", "hyu", "hyo",
        "mya", "myu", "myo",
        "rya", "ryu", "ryo",
        "gya", "gyu", "gyo",
        "ja", "ju", "jo", "jya", "jyu", "jyo",
        "bya", "byu", "byo",
        "pya", "pyu", "pyo",
        "la", "li", "lu", "le", "lo",
        "xa", "xi", "xu", "xe", "xo",
        "ltu", "xtu", "lya", "lyu", "lyo",
        "xya", "xyu", "xyo",
        "-"
    };
    return keys;
}

bool has_romaji_prefix(const std::string& text)
{
    return std::any_of(romaji_keys().begin(), romaji_keys().end(), [&](const auto& key) {
        return key.starts_with(text);
    });
}

bool has_longer_romaji_prefix(const std::string& text)
{
    return std::any_of(romaji_keys().begin(), romaji_keys().end(), [&](const auto& key) {
        return key.size() > text.size() && key.starts_with(text);
    });
}

bool has_romaji_key(const std::string& text)
{
    return std::find(romaji_keys().begin(), romaji_keys().end(), text) != romaji_keys().end();
}

void append_utf8(std::string& out, char32_t codepoint)
{
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::vector<char32_t> utf8_to_codepoints(const std::string& text)
{
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x7f) {
            out.push_back(c);
            ++i;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
            out.push_back(((c & 0x1f) << 6)
                          | (static_cast<unsigned char>(text[i + 1]) & 0x3f));
            i += 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
            out.push_back(((c & 0x0f) << 12)
                          | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6)
                          | (static_cast<unsigned char>(text[i + 2]) & 0x3f));
            i += 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
            out.push_back(((c & 0x07) << 18)
                          | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12)
                          | ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6)
                          | (static_cast<unsigned char>(text[i + 3]) & 0x3f));
            i += 4;
        } else {
            ++i;
        }
    }
    return out;
}

std::string hiragana_to_katakana(const std::string& text)
{
    std::string out;
    for (char32_t codepoint : utf8_to_codepoints(text)) {
        if (codepoint >= 0x3041 && codepoint <= 0x3096) {
            codepoint += 0x60;
        } else if (codepoint == 0x309d || codepoint == 0x309e) {
            codepoint += 0x60;
        }
        append_utf8(out, codepoint);
    }
    return out;
}

std::string halfwidth_katakana_char(char32_t codepoint)
{
    switch (codepoint) {
    case U'。': return "｡";
    case U'、': return "､";
    case U'・': return "･";
    case U'ー': return "ｰ";
    case U'「': return "｢";
    case U'」': return "｣";
    case U'ァ': return "ｧ";
    case U'ア': return "ｱ";
    case U'ィ': return "ｨ";
    case U'イ': return "ｲ";
    case U'ゥ': return "ｩ";
    case U'ウ': return "ｳ";
    case U'ヴ': return "ｳﾞ";
    case U'ェ': return "ｪ";
    case U'エ': return "ｴ";
    case U'ォ': return "ｫ";
    case U'オ': return "ｵ";
    case U'カ': return "ｶ";
    case U'ガ': return "ｶﾞ";
    case U'キ': return "ｷ";
    case U'ギ': return "ｷﾞ";
    case U'ク': return "ｸ";
    case U'グ': return "ｸﾞ";
    case U'ケ': return "ｹ";
    case U'ゲ': return "ｹﾞ";
    case U'コ': return "ｺ";
    case U'ゴ': return "ｺﾞ";
    case U'サ': return "ｻ";
    case U'ザ': return "ｻﾞ";
    case U'シ': return "ｼ";
    case U'ジ': return "ｼﾞ";
    case U'ス': return "ｽ";
    case U'ズ': return "ｽﾞ";
    case U'セ': return "ｾ";
    case U'ゼ': return "ｾﾞ";
    case U'ソ': return "ｿ";
    case U'ゾ': return "ｿﾞ";
    case U'タ': return "ﾀ";
    case U'ダ': return "ﾀﾞ";
    case U'チ': return "ﾁ";
    case U'ヂ': return "ﾁﾞ";
    case U'ッ': return "ｯ";
    case U'ツ': return "ﾂ";
    case U'ヅ': return "ﾂﾞ";
    case U'テ': return "ﾃ";
    case U'デ': return "ﾃﾞ";
    case U'ト': return "ﾄ";
    case U'ド': return "ﾄﾞ";
    case U'ナ': return "ﾅ";
    case U'ニ': return "ﾆ";
    case U'ヌ': return "ﾇ";
    case U'ネ': return "ﾈ";
    case U'ノ': return "ﾉ";
    case U'ハ': return "ﾊ";
    case U'バ': return "ﾊﾞ";
    case U'パ': return "ﾊﾟ";
    case U'ヒ': return "ﾋ";
    case U'ビ': return "ﾋﾞ";
    case U'ピ': return "ﾋﾟ";
    case U'フ': return "ﾌ";
    case U'ブ': return "ﾌﾞ";
    case U'プ': return "ﾌﾟ";
    case U'ヘ': return "ﾍ";
    case U'ベ': return "ﾍﾞ";
    case U'ペ': return "ﾍﾟ";
    case U'ホ': return "ﾎ";
    case U'ボ': return "ﾎﾞ";
    case U'ポ': return "ﾎﾟ";
    case U'マ': return "ﾏ";
    case U'ミ': return "ﾐ";
    case U'ム': return "ﾑ";
    case U'メ': return "ﾒ";
    case U'モ': return "ﾓ";
    case U'ャ': return "ｬ";
    case U'ヤ': return "ﾔ";
    case U'ュ': return "ｭ";
    case U'ユ': return "ﾕ";
    case U'ョ': return "ｮ";
    case U'ヨ': return "ﾖ";
    case U'ラ': return "ﾗ";
    case U'リ': return "ﾘ";
    case U'ル': return "ﾙ";
    case U'レ': return "ﾚ";
    case U'ロ': return "ﾛ";
    case U'ヮ': return "ﾜ";
    case U'ワ': return "ﾜ";
    case U'ヰ': return "ｲ";
    case U'ヱ': return "ｴ";
    case U'ヲ': return "ｦ";
    case U'ン': return "ﾝ";
    case U'ヵ': return "ｶ";
    case U'ヶ': return "ｹ";
    default:
        std::string out;
        append_utf8(out, codepoint);
        return out;
    }
}

std::string katakana_to_halfwidth(const std::string& text)
{
    std::string out;
    for (char32_t codepoint : utf8_to_codepoints(text)) {
        out += halfwidth_katakana_char(codepoint);
    }
    return out;
}

std::string ascii_to_fullwidth(const std::string& text)
{
    std::string out;
    for (unsigned char ch : text) {
        if (ch == ' ') {
            append_utf8(out, 0x3000);
        } else if (ch >= 0x21 && ch <= 0x7e) {
            append_utf8(out, 0xff01 + (ch - 0x21));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

void append_output_variants(std::vector<kanaria::candidate>& candidates,
                            const std::string& kana_utf8,
                            const std::string& roman)
{
    const std::string katakana = hiragana_to_katakana(kana_utf8);
    append_candidate_unique(candidates, kanaria::candidate{katakana, kana_utf8, 0, {}, 0});
    append_candidate_unique(candidates,
                            kanaria::candidate{katakana_to_halfwidth(katakana),
                                               kana_utf8, 0, {}, 0});
    if (!roman.empty()) {
        append_candidate_unique(candidates,
                                kanaria::candidate{ascii_to_fullwidth(roman),
                                                   kana_utf8, 0, {}, 0});
        append_candidate_unique(candidates, kanaria::candidate{roman, kana_utf8, 0, {}, 0});
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

InputState::InputState() = default;

bool InputState::ready() const { return true; }

bool InputState::ensure_engine()
{
    if (!engine_) {
        engine_ = kanaria::engine_create();
    }
    return engine_ != nullptr;
}

void InputState::flush_pending_roman(bool force)
{
    while (!pending_roman_.empty()) {
        if (is_doubled_consonant(pending_roman_)) {
            kana_utf8_ += kanaria::romaji_to_hiragana("ltu");
            pending_roman_.erase(0, 1);
            continue;
        }

        if (has_romaji_key(pending_roman_)) {
            if (!force && has_longer_romaji_prefix(pending_roman_)) {
                return;
            }
            kana_utf8_ += kanaria::romaji_to_hiragana(pending_roman_);
            pending_roman_.clear();
            continue;
        }

        if (has_romaji_prefix(pending_roman_)) {
            return;
        }

        bool consumed = false;
        for (std::size_t len = pending_roman_.size(); len > 0; --len) {
            const std::string prefix = pending_roman_.substr(0, len);
            if (!has_romaji_key(prefix)) {
                continue;
            }
            kana_utf8_ += kanaria::romaji_to_hiragana(prefix);
            pending_roman_.erase(0, len);
            consumed = true;
            break;
        }
        if (consumed) {
            continue;
        }

        if (!force) {
            return;
        }
        kana_utf8_.push_back(pending_roman_.front());
        pending_roman_.erase(0, 1);
    }
}

std::string InputState::composing_kana() const
{
    if (pending_roman_.empty()) {
        return kana_utf8_;
    }
    return kana_utf8_ + kanaria::romaji_to_hiragana(pending_roman_);
}

bool InputState::key_ascii(unsigned int ch)
{
    if (ch <= 0x20 || ch > 0x7e) {
        return false;
    }
    const char value = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    roman_.push_back(value);
    pending_roman_.push_back(value);
    clear_conversion();
    flush_pending_roman(false);
    refresh_predictions();
    return true;
}

bool InputState::backspace()
{
    if (converting_) {
        clear_conversion();
        refresh_predictions();
        return true;
    }
    if (pending_roman_.empty() && kana_utf8_.empty()) {
        return false;
    }
    if (!pending_roman_.empty()) {
        pending_roman_.pop_back();
    } else {
        pop_utf8_char(kana_utf8_);
    }
    if (!roman_.empty()) {
        roman_.pop_back();
    }
    clear_conversion();
    refresh_predictions();
    return true;
}

void InputState::cancel()
{
    roman_.clear();
    pending_roman_.clear();
    kana_utf8_.clear();
    clear_conversion();
}

bool InputState::start_conversion()
{
    clear_conversion();
    flush_pending_roman(true);
    if (!make_kana()) {
        return false;
    }
    if (!ensure_engine()) {
        return false;
    }

    auto best = kanaria::engine_convert_best(*engine_, kana_utf8_);
    if (best && !best->elements.empty()) {
        clauses_ = best->elements;
    }

    clause_index_ = 0;
    refresh_clause_candidates();
    converting_ = true;
    return true;
}

bool InputState::next_candidate()
{
    if (candidates_.empty()) {
        return false;
    }
    candidate_index_ = (candidate_index_ + 1) % static_cast<int>(candidates_.size());
    ensure_candidate_visible();
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::prev_candidate()
{
    if (candidates_.empty()) {
        return false;
    }
    candidate_index_ = (candidate_index_ + static_cast<int>(candidates_.size()) - 1)
        % static_cast<int>(candidates_.size());
    ensure_candidate_visible();
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::next_page()
{
    if (candidates_.empty() || page_start_ + candidate_page_size >= candidate_count()) {
        return false;
    }
    page_start_ += candidate_page_size;
    candidate_index_ = page_start_;
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::prev_page()
{
    if (candidates_.empty() || page_start_ == 0) {
        return false;
    }
    page_start_ = std::max(0, page_start_ - candidate_page_size);
    candidate_index_ = page_start_;
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::set_candidate_page(int page)
{
    const int page_count = candidate_page_count();
    if (page < 0 || page >= page_count) {
        return false;
    }
    page_start_ = page * candidate_page_size;
    candidate_index_ = page_start_;
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::select_candidate(int index)
{
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return false;
    }
    candidate_index_ = index;
    ensure_candidate_visible();
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::select_visible_candidate(int index)
{
    if (index < 0 || index >= visible_candidate_count()) {
        return false;
    }
    candidate_index_ = page_start_ + index;
    if (!predicting_) {
        converting_ = true;
    }
    return true;
}

bool InputState::next_clause()
{
    if (!converting_ || clauses_.empty() || clause_index_ + 1 >= clauses_.size()) {
        return false;
    }
    apply_current_candidate_to_clause();
    ++clause_index_;
    return refresh_clause_candidates();
}

bool InputState::prev_clause()
{
    if (!converting_ || clauses_.empty() || clause_index_ == 0) {
        return false;
    }
    apply_current_candidate_to_clause();
    --clause_index_;
    return refresh_clause_candidates();
}

std::string InputState::preedit()
{
    if (converting_ && !candidates_.empty()) {
        return composed_candidate();
    }
    return composing_kana();
}

std::string InputState::commit()
{
    std::string text;
    if (converting_ && !candidates_.empty()) {
        const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
        text = composed_candidate();
        if (ensure_engine()) {
            kanaria::engine_learn_candidate(*engine_, selected);
        }
    } else if (predicting_ && !candidates_.empty()) {
        const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
        text = selected.candidate;
        if (ensure_engine()) {
            kanaria::engine_learn_candidate(*engine_, selected);
        }
    } else {
        flush_pending_roman(true);
        text = kana_utf8_;
    }
    cancel();
    return text;
}

int InputState::candidate_count() const { return static_cast<int>(candidates_.size()); }
int InputState::candidate_index() const { return candidate_index_; }
int InputState::candidate_page_start() const { return page_start_; }
int InputState::candidate_page_index() const { return candidate_index_ - page_start_; }
int InputState::candidate_page_count() const
{
    const int count = candidate_count();
    return count == 0 ? 0 : (count + candidate_page_size - 1) / candidate_page_size;
}
int InputState::visible_candidate_count() const
{
    if (page_start_ < 0 || page_start_ >= candidate_count()) {
        return 0;
    }
    return std::min(candidate_page_size, candidate_count() - page_start_);
}
bool InputState::is_converting() const { return converting_; }
bool InputState::is_predicting() const { return predicting_; }
bool InputState::has_candidate_window() const { return converting_ || predicting_; }
bool InputState::has_text() const
{
    return !pending_roman_.empty() || !kana_utf8_.empty() || converting_;
}

std::string InputState::candidate(int index) const
{
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return {};
    }
    return candidates_[static_cast<std::size_t>(index)].candidate;
}

std::string InputState::visible_candidate(int index) const
{
    if (index < 0 || index >= visible_candidate_count()) {
        return {};
    }
    return candidate(page_start_ + index);
}

std::size_t InputState::active_clause_begin() const
{
    if (!converting_ || clauses_.empty() || clause_index_ >= clauses_.size()) {
        return 0;
    }

    std::size_t offset = 0;
    for (std::size_t i = 0; i < clause_index_; ++i) {
        offset += clauses_[i].value.candidate.size();
    }
    return offset;
}

std::size_t InputState::active_clause_end() const
{
    if (!converting_) {
        return 0;
    }
    if (clauses_.empty() || clause_index_ >= clauses_.size()) {
        if (candidates_.empty()) {
            return 0;
        }
        return candidates_[static_cast<std::size_t>(candidate_index_)].candidate.size();
    }

    const std::size_t begin = active_clause_begin();
    if (candidates_.empty()) {
        return begin + clauses_[clause_index_].value.candidate.size();
    }
    return begin + candidates_[static_cast<std::size_t>(candidate_index_)].candidate.size();
}

void InputState::apply_current_candidate_to_clause()
{
    if (clauses_.empty() || clause_index_ >= clauses_.size() || candidates_.empty()) {
        return;
    }

    const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
    if (selected.stroke == kana_utf8_ && clauses_.size() > 1) {
        return;
    }

    auto& value = clauses_[clause_index_].value;
    value.candidate = selected.candidate;
    value.stroke = selected.stroke;
    value.frequency = selected.frequency;
    value.connection = selected.connection;
    value.attribute = selected.attribute;
}

std::string InputState::composed_candidate() const
{
    if (candidates_.empty()) {
        return {};
    }

    const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
    if (clauses_.empty() || selected.stroke == kana_utf8_) {
        return selected.candidate;
    }

    std::string text;
    for (std::size_t i = 0; i < clauses_.size(); ++i) {
        if (i == clause_index_) {
            text += selected.candidate;
        } else {
            text += clauses_[i].value.candidate;
        }
    }
    return text;
}

std::size_t InputState::clause_position() const
{
    std::size_t position = 0;
    for (std::size_t i = 0; i < clause_index_ && i < clauses_.size(); ++i) {
        position += utf8_codepoint_count(clauses_[i].value.stroke);
    }
    return position;
}

std::size_t InputState::clause_length() const
{
    if (clauses_.empty() || clause_index_ >= clauses_.size()) {
        return utf8_codepoint_count(kana_utf8_);
    }
    return utf8_codepoint_count(clauses_[clause_index_].value.stroke);
}

bool InputState::refresh_predictions()
{
    candidates_.clear();
    candidate_index_ = 0;
    page_start_ = 0;
    predicting_ = false;

    const std::string kana = composing_kana();
    if (converting_ || kana.empty()) {
        return false;
    }
    if (!roman_.empty() && kana == roman_) {
        return false;
    }
    if (!ensure_engine()) {
        return false;
    }

    candidates_ = kanaria::engine_predict(*engine_, kana, 64);
    predicting_ = !candidates_.empty();
    ensure_candidate_visible();
    return predicting_;
}

bool InputState::refresh_clause_candidates()
{
    candidates_.clear();
    candidate_index_ = 0;
    page_start_ = 0;
    predicting_ = false;

    if (!ensure_engine()) {
        return false;
    }

    const std::size_t position = clause_position();
    const std::size_t length = clause_length();
    if (length > 0) {
        auto clause_candidates =
            kanaria::engine_get_clause_candidates(*engine_, kana_utf8_, position, length, 64);
        std::vector<kanaria::candidate> deferred_candidates;
        for (const auto& value : clause_candidates) {
            if (value.candidate == "Kanaria-0.1") {
                deferred_candidates.push_back(value);
                continue;
            }
            append_candidate_unique(candidates_, value);
        }
        if (clause_index_ == 0) {
            append_output_variants(candidates_, kana_utf8_, roman_);
        }
        for (const auto& value : deferred_candidates) {
            append_candidate_unique(candidates_, value);
        }
    } else if (clause_index_ == 0) {
        append_output_variants(candidates_, kana_utf8_, roman_);
    }

    if (candidates_.empty()) {
        if (!clauses_.empty() && clause_index_ < clauses_.size()) {
            const auto& value = clauses_[clause_index_].value;
            candidates_.push_back(kanaria::candidate{value.candidate,
                                                     value.stroke,
                                                     value.frequency,
                                                     value.connection,
                                                     value.attribute});
        } else {
            candidates_.push_back(kanaria::candidate{kana_utf8_, kana_utf8_, 0, {}, 0});
        }
    }
    ensure_candidate_visible();
    return true;
}

void InputState::ensure_candidate_visible()
{
    if (candidates_.empty()) {
        page_start_ = 0;
        candidate_index_ = 0;
        return;
    }

    const int count = candidate_count();
    candidate_index_ = std::clamp(candidate_index_, 0, count - 1);
    page_start_ = std::clamp(page_start_, 0, ((count - 1) / candidate_page_size) * candidate_page_size);
    if (candidate_index_ < page_start_ || candidate_index_ >= page_start_ + candidate_page_size) {
        page_start_ = (candidate_index_ / candidate_page_size) * candidate_page_size;
    }
}

void InputState::clear_conversion()
{
    clauses_.clear();
    candidates_.clear();
    candidate_index_ = 0;
    page_start_ = 0;
    clause_index_ = 0;
    converting_ = false;
    predicting_ = false;
}

bool InputState::make_kana()
{
    flush_pending_roman(true);
    return !kana_utf8_.empty();
}

} // namespace kanaria_frontend
