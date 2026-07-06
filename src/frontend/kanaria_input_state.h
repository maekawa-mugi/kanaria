#ifndef KANARIA_FRONTEND_INPUT_STATE_H
#define KANARIA_FRONTEND_INPUT_STATE_H

#include "kanaria.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kanaria_frontend {

std::size_t utf8_codepoint_count(const std::string& s);
void pop_utf8_char(std::string& s);

class InputState {
public:
    InputState();

    bool ready() const;
    bool key_ascii(unsigned int ch);
    bool backspace();
    void cancel();
    bool start_conversion();
    bool next_candidate();
    bool prev_candidate();
    bool next_clause();
    bool prev_clause();
    bool select_candidate(int index);

    std::string preedit();
    std::string commit();

    int candidate_count() const;
    int candidate_index() const;
    bool is_converting() const;
    bool has_text() const;
    std::string candidate(int index) const;
    std::size_t active_clause_begin() const;
    std::size_t active_clause_end() const;

private:
    bool ensure_engine();
    void apply_current_candidate_to_clause();
    std::string composed_candidate() const;
    bool refresh_clause_candidates();
    std::size_t clause_position() const;
    std::size_t clause_length() const;
    void clear_conversion();
    bool make_kana();

    kanaria::engine_ptr engine_;
    std::string roman_;
    std::string kana_utf8_;
    std::vector<kanaria::clause> clauses_;
    std::vector<kanaria::candidate> candidates_;
    int candidate_index_ = 0;
    std::size_t clause_index_ = 0;
    bool converting_ = false;
};

} // namespace kanaria_frontend

#endif // KANARIA_FRONTEND_INPUT_STATE_H
