/*
 * Std C++23 porting layer for the Qt OpenWnn library.
 *
 * Copyright (C) 2015 The Qt Company
 * Copyright (C) 2008-2012 OMRON SOFTWARE Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef OPENWNN_STD_H
#define OPENWNN_STD_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace openwnn {

struct pos {
    int left = 0;
    int right = 0;
};

struct word {
    int id = 0;
    std::string candidate;
    std::string stroke;
    int frequency = 0;
    pos part_of_speech;
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
    pos part_of_speech;
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

std::string romaji_to_hiragana(const std::string& ascii_romaji);

} // namespace openwnn

#endif // OPENWNN_STD_H
