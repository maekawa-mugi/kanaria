/*
 * Std C++23 porting layer for the Qt Kanaria library.
 *
 * Copyright (C) 2015 The Qt Company
 * Copyright (C) 2008-2012 OMRON SOFTWARE Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef KANARIA_H
#define KANARIA_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kanaria {

struct connector {
    int left_id = 0;
    int right_id = 0;
};

struct word {
    int id = 0;
    std::string candidate;
    std::string stroke;
    int frequency = 0;
    connector connection;
    int attribute = 0;
};

struct clause {
    word value;
};

struct sentence {
    word value;
    std::vector<clause> elements;
};

struct candidate {
    std::string candidate;
    std::string stroke;
    int frequency = 0;
    connector connection;
    int attribute = 0;
};

struct engine;

struct engine_deleter {
    void operator()(engine* e) const noexcept;
};

using engine_ptr = std::unique_ptr<engine, engine_deleter>;

engine_ptr engine_create();
void engine_reset(engine& e);

std::vector<candidate> engine_predict(engine& e, const std::string& utf8_hiragana,
                                      std::size_t limit = 100);
std::vector<candidate> engine_convert(engine& e, const std::string& utf8_hiragana,
                                      std::size_t limit = 100);

// OpenWnn-compatible conversion APIs. Sentence conversion is best-1; candidate
// enumeration belongs to a selected clause rather than to the whole sentence.
std::optional<sentence> engine_convert_best(engine& e, const std::string& utf8_hiragana);
std::vector<candidate> engine_get_clause_candidates(engine& e,
                                                    const std::string& utf8_hiragana,
                                                    std::size_t clause_position,
                                                    std::size_t clause_length,
                                                    std::size_t limit = 100);
std::optional<sentence> engine_resize_clause(engine& e,
                                             const std::string& utf8_hiragana,
                                             std::size_t clause_position,
                                             std::size_t clause_length);

// User and learning entries use the same frequency/connector model as bundled
// dictionary words. Mutations invalidate conversion caches automatically.
bool engine_add_user_word(engine& e, const word& entry);
bool engine_remove_user_word(engine& e, const word& entry);
void engine_clear_user_words(engine& e);
bool engine_learn_candidate(engine& e, const candidate& selected, int frequency_delta = 1);
void engine_clear_learning(engine& e);

std::string romaji_to_hiragana(const std::string& ascii_romaji);

} // namespace kanaria

#endif // KANARIA_H
