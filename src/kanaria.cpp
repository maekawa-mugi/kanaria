/*
 * Std C++23 porting layer for the Qt Kanaria library.
 *
 * Copyright (C) 2015 The Qt Company
 * Copyright (C) 2008-2012 OMRON SOFTWARE Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "kanaria.h"

extern "C" {
#include "nj_lib.h"
#include "nj_err.h"
#include "nj_ext.h"
#include "nj_dic.h"

extern const uint32_t dic_size[];
extern const uint8_t dic_type[];
extern const uint8_t *const dic_data[];
extern const uint8_t *const con_data[];
}

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace kanaria {
namespace {

static_assert(sizeof(NJ_CHAR) == sizeof(char16_t));
static_assert(std::endian::native == std::endian::little,
              "Kanaria's internal UTF-16 representation requires little-endian byte order");

constexpr int nj_func_set_dictionary_parameters = 0x00FA;
constexpr int nj_func_search_word = 0x003C;

constexpr int nj_err_invalid_param = 0x7B00;

constexpr int flag_none = 0x00;
constexpr int flag_enable_cursor = 0x01;
constexpr int flag_enable_result = 0x02;

constexpr int freq_learn = 600;
constexpr int freq_user = 500;
constexpr int max_input_length = 50;
constexpr int max_output_length = 50;
constexpr int clause_cost = -1000;
constexpr std::size_t max_cache_entries = 4096;

constexpr int dictionary_prediction = 0;
constexpr int dictionary_suffix = 2;
constexpr int dictionary_single_kanji = 3;
constexpr int dictionary_independent = 4;
constexpr int dictionary_ancillary = 5;

enum search_operation {
    search_exact,
    search_prefix,
    search_link,
};

enum search_order {
    order_by_frequency,
    order_by_key,
};

enum connector_type {
    connector_type_v1,
    connector_type_v2,
    connector_type_v3,
    connector_type_sentence_start,
    connector_type_single_kanji,
    connector_type_number,
    connector_type_noun,
    connector_type_person_name,
    connector_type_place_name,
    connector_type_symbol,
};

struct nj_work {
    NJ_DIC_HANDLE dic_handle[NJ_MAX_DIC];
    uint32_t dic_size[NJ_MAX_DIC];
    uint8_t dic_type[NJ_MAX_DIC];
    NJ_CHAR key_string[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_RESULT result;
    NJ_CURSOR cursor;
    NJ_SEARCH_CACHE search_cache[NJ_MAX_DIC];
    NJ_DIC_SET dic_set;
    NJ_CLASS wnn_class;
    NJ_CHARSET approx_set;
    NJ_CHAR previous_stroke[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_CHAR previous_candidate[NJ_MAX_RESULT_LEN + NJ_TERM_LEN];
    uint8_t flag;
};

struct dictionary_work {
    nj_work work{};
};

struct connection_table {
    std::size_t right_count = 0;
    std::vector<unsigned char> allowed;
};

struct clause_converter {
    dictionary_work* dictionary = nullptr;
    std::unordered_map<std::string, std::vector<word>> indep_word_bag;
    std::unordered_map<std::string, std::vector<word>> all_indep_word_bag;
    std::unordered_map<std::string, std::vector<word>> fzk_patterns;
    connection_table connect_matrix;
    const std::vector<word>* user_words = nullptr;
    const std::vector<word>* learned_words = nullptr;
    std::uint64_t cache_version = 0;
    connector default_connector;
    connector end_clause_connector_1;
    connector end_clause_connector_2;
    connector end_clause_connector_3;
};

struct utf8_input_view {
    std::string_view bytes;
    std::vector<std::uint32_t> offsets;

    explicit utf8_input_view(const std::string& input) : bytes(input)
    {
        offsets.reserve(input.size() + 1);
        offsets.push_back(0);
        for (std::size_t i = 0; i < input.size();) {
            const unsigned char c = static_cast<unsigned char>(input[i]);
            std::size_t step = 1;
            if ((c & 0xE0) == 0xC0) {
                step = 2;
            } else if ((c & 0xF0) == 0xE0) {
                step = 3;
            } else if ((c & 0xF8) == 0xF0) {
                step = 4;
            }
            i = std::min(i + step, input.size());
            offsets.push_back(static_cast<std::uint32_t>(i));
        }
    }

    std::size_t size() const { return offsets.size() - 1; }

    std::string span(std::size_t begin, std::size_t end) const
    {
        if (begin >= end || end > size()) {
            return {};
        }
        return std::string(bytes.substr(offsets[begin], offsets[end] - offsets[begin]));
    }
};

static std::size_t utf8_codepoint_count(const std::string& s)
{
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

static bool is_utf8_continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

static std::optional<std::size_t> validated_utf8_code_unit_count(const std::string& s)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t codepoint = 0;
        std::size_t step = 0;

        if (c <= 0x7F) {
            codepoint = c;
            step = 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= s.size() || !is_utf8_continuation(static_cast<unsigned char>(s[i + 1]))) {
                return std::nullopt;
            }
            codepoint = ((c & 0x1F) << 6)
                | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            step = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= s.size()
                || !is_utf8_continuation(static_cast<unsigned char>(s[i + 1]))
                || !is_utf8_continuation(static_cast<unsigned char>(s[i + 2]))) {
                return std::nullopt;
            }
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 > 0x9F)) {
                return std::nullopt;
            }
            codepoint = ((c & 0x0F) << 12)
                | ((c1 & 0x3F) << 6)
                | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            step = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= s.size()
                || !is_utf8_continuation(static_cast<unsigned char>(s[i + 1]))
                || !is_utf8_continuation(static_cast<unsigned char>(s[i + 2]))
                || !is_utf8_continuation(static_cast<unsigned char>(s[i + 3]))) {
                return std::nullopt;
            }
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 > 0x8F)) {
                return std::nullopt;
            }
            codepoint = ((c & 0x07) << 18)
                | ((c1 & 0x3F) << 12)
                | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6)
                | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
            step = 4;
        } else {
            return std::nullopt;
        }

        if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F)) {
            return std::nullopt;
        }

        i += step;
        count += codepoint > 0xFFFF ? 2 : 1;
    }
    return count;
}

static std::string to_ascii_lower(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

static std::u16string utf8_to_utf16(const std::string& src_string)
{
    std::u16string out;
    out.reserve(src_string.size());

    for (std::size_t i = 0; i < src_string.size();) {
        unsigned char c = static_cast<unsigned char>(src_string[i]);
        char32_t codepoint = 0;
        std::size_t step = 0;

        if (c <= 0x7F) {
            codepoint = c;
            step = 1;
        } else if (c <= 0xDF) {
            codepoint = ((c & 0x1F) << 6)
                | (static_cast<unsigned char>(src_string[i + 1]) & 0x3F);
            step = 2;
        } else if (c <= 0xEF) {
            codepoint = ((c & 0x0F) << 12)
                | ((static_cast<unsigned char>(src_string[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(src_string[i + 2]) & 0x3F);
            step = 3;
        } else {
            codepoint = ((c & 0x07) << 18)
                | ((static_cast<unsigned char>(src_string[i + 1]) & 0x3F) << 12)
                | ((static_cast<unsigned char>(src_string[i + 2]) & 0x3F) << 6)
                | (static_cast<unsigned char>(src_string[i + 3]) & 0x3F);
            step = 4;
        }

        if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
        }
        i += step;
    }
    return out;
}

static void convert_string_to_nj_char(NJ_CHAR* dst, const std::string& src_string,
                                      int max_chars, int capacity_chars)
{
    if (capacity_chars <= 0) {
        return;
    }

    const int max_output_chars = std::min(max_chars, capacity_chars - 1);
    const std::u16string utf16 = utf8_to_utf16(src_string);
    int o = 0;
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        if (o >= max_output_chars) {
            break;
        }
        if (NJ_CHAR_IS_HIGH_SURROGATE(utf16[i]) && o + 1 >= max_output_chars) {
            break;
        }
        dst[o++] = static_cast<NJ_CHAR>(utf16[i]);
    }

    dst[o] = NJ_CHAR_NUL;
}

static void append_codepoint_as_utf8(std::string& dst, char32_t codepoint)
{
    if (codepoint <= 0x7F) {
        dst.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        dst.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        dst.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        dst.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        dst.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        dst.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        dst.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        dst.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        dst.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        dst.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

static std::string convert_nj_char_to_string(const NJ_CHAR* src, int max_chars)
{
    std::string dst;
    dst.reserve((NJ_MAX_LEN + NJ_MAX_RESULT_LEN + NJ_TERM_LEN) * 4 + 1);

    for (int i = 0; i < max_chars && src[i] != NJ_CHAR_NUL;) {
        char32_t codepoint = src[i++];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (i >= max_chars || !NJ_CHAR_IS_LOW_SURROGATE(src[i])) {
                break;
            }
            codepoint = 0x10000
                + ((codepoint - 0xD800) << 10)
                + (src[i++] - 0xDC00);
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            break;
        }
        append_codepoint_as_utf8(dst, codepoint);
    }

    return dst;
}

static void dictionary_init(dictionary_work& d)
{
    std::memset(&d.work, 0, sizeof(d.work));
    for (int i = 0; i < NJ_MAX_DIC; i++) {
        d.work.dic_handle[i] = dic_data[i];
        d.work.dic_size[i] = dic_size[i];
        d.work.dic_type[i] = dic_type[i];
    }
    d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] = con_data[0];
    njx_init(&d.work.wnn_class);
}

static void clear_dictionary_structure(NJ_DIC_INFO* dic_info)
{
    dic_info->type = 0;
    dic_info->handle = nullptr;
    dic_info->dic_freq[NJ_MODE_TYPE_HENKAN].base = 0;
    dic_info->dic_freq[NJ_MODE_TYPE_HENKAN].high = 0;
}

static void dictionary_clear(dictionary_work& d)
{
    for (int i = 0; i < NJ_MAX_DIC; i++) {
        clear_dictionary_structure(&d.work.dic_set.dic[i]);
    }
    d.work.flag = flag_none;
    std::memset(d.work.dic_set.keyword, 0, sizeof(d.work.dic_set.keyword));
}

static int dictionary_set(dictionary_work& d, int index, int base, int high)
{
    if (index < 0) {
        return 0;
    }
    if ((index > NJ_MAX_DIC - 1) || (base < -1 || base > 1000) || (high < -1 || high > 1000)) {
        return NJ_SET_ERR_VAL(nj_func_set_dictionary_parameters, nj_err_invalid_param);
    }

    if (base < 0 || high < 0 || base > high) {
        clear_dictionary_structure(&d.work.dic_set.dic[index]);
    } else {
        d.work.dic_set.dic[index].type = d.work.dic_type[index];
        d.work.dic_set.dic[index].handle = d.work.dic_handle[index];
        d.work.dic_set.dic[index].srhCache = &d.work.search_cache[index];
        d.work.dic_set.dic[index].dic_freq[NJ_MODE_TYPE_HENKAN].base = base;
        d.work.dic_set.dic[index].dic_freq[NJ_MODE_TYPE_HENKAN].high = high;
    }
    d.work.flag = flag_none;
    return 0;
}

static void dictionary_clear_approx(dictionary_work& d)
{
    d.work.flag = flag_none;
    d.work.approx_set.charset_count = 0;
    for (int i = 0; i < NJ_MAX_CHARSET; i++) {
        d.work.approx_set.from[i] = nullptr;
        d.work.approx_set.to[i] = nullptr;
    }
    std::memset(d.work.dic_set.keyword, 0, sizeof(d.work.dic_set.keyword));
}

static void dictionary_clear_result(dictionary_work& d)
{
    std::memset(&d.work.result, 0, sizeof(NJ_RESULT));
    std::memset(d.work.previous_stroke, 0, sizeof(d.work.previous_stroke));
    std::memset(d.work.previous_candidate, 0, sizeof(d.work.previous_candidate));
}

static int dictionary_search(dictionary_work& d, search_operation operation, search_order order, const std::string& key)
{
    if (key.empty() || utf8_codepoint_count(key) > NJ_MAX_LEN) {
        d.work.flag &= ~flag_enable_cursor;
        d.work.flag &= ~flag_enable_result;
        return key.empty() ? NJ_SET_ERR_VAL(nj_func_search_word, nj_err_invalid_param) : 0;
    }

    dictionary_clear_result(d);
    convert_string_to_nj_char(d.work.key_string, key, NJ_MAX_LEN, NJ_MAX_LEN + NJ_TERM_LEN);
    std::memset(&d.work.cursor, 0, sizeof(NJ_CURSOR));
    d.work.cursor.cond.operation = operation;
    d.work.cursor.cond.mode = order;
    d.work.cursor.cond.ds = &d.work.dic_set;
    d.work.cursor.cond.yomi = d.work.key_string;
    d.work.cursor.cond.charset = &d.work.approx_set;

    if (operation == search_link) {
        d.work.cursor.cond.yomi = d.work.previous_stroke;
        d.work.cursor.cond.kanji = d.work.previous_candidate;
    }

    std::memcpy(&d.work.wnn_class.dic_set, &d.work.dic_set, sizeof(NJ_DIC_SET));
    int result = njx_search_word(&d.work.wnn_class, &d.work.cursor);
    if (result == 1) {
        d.work.flag |= flag_enable_cursor;
    } else {
        d.work.flag &= ~flag_enable_cursor;
    }
    d.work.flag &= ~flag_enable_result;
    return result;
}

static int dictionary_get_next_raw(dictionary_work& d, int length)
{
    if (!(d.work.flag & flag_enable_cursor)) {
        return 0;
    }

    int result = 0;
    if (length <= 0) {
        result = njx_get_word(&d.work.wnn_class, &d.work.cursor, &d.work.result);
    } else {
        do {
            result = njx_get_word(&d.work.wnn_class, &d.work.cursor, &d.work.result);
            if (length == (NJ_GET_YLEN_FROM_STEM(&d.work.result.word) + NJ_GET_YLEN_FROM_FZK(&d.work.result.word))) {
                break;
            }
        } while (result > 0);
    }

    if (result > 0) {
        d.work.flag |= flag_enable_result;
    } else {
        d.work.flag &= ~flag_enable_result;
    }
    return result;
}

static std::optional<word> dictionary_get_next(dictionary_work& d, int length = 0)
{
    if (dictionary_get_next_raw(d, length) <= 0) {
        return std::nullopt;
    }

    NJ_CHAR stroke[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_CHAR candidate_text[NJ_MAX_RESULT_LEN + NJ_TERM_LEN];
    if (njx_get_stroke(&d.work.wnn_class, &d.work.result, stroke, sizeof(stroke)) < 0) {
        return std::nullopt;
    }
    if (njx_get_candidate(&d.work.wnn_class, &d.work.result, candidate_text, sizeof(candidate_text)) < 0) {
        return std::nullopt;
    }

    word out;
    out.candidate = convert_nj_char_to_string(candidate_text, NJ_MAX_RESULT_LEN);
    out.stroke = convert_nj_char_to_string(stroke, NJ_MAX_LEN);
    out.frequency = d.work.result.word.stem.hindo;
    out.connection.left_id = NJ_GET_LEFT_CONNECTION_ID_FROM_STEM(&d.work.result.word);
    out.connection.right_id = NJ_GET_RIGHT_CONNECTION_ID_FROM_STEM(&d.work.result.word);
    return out;
}

static std::vector<unsigned char> dictionary_get_connect_array(dictionary_work& d, int left_pos)
{
    uint16_t left_count = 0;
    uint16_t right_count = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return {};
    }
    njd_r_get_connection_counts(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &left_count, &right_count);
    if (left_pos < 0 || left_pos > left_count) {
        return {};
    }

    std::vector<unsigned char> result(static_cast<std::size_t>(right_count) + 1, 0);
    const uint8_t* row = nullptr;
    if (left_pos > 0) {
        njd_r_get_connection_row(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], left_pos, NJ_RULE_TYPE_FTOB, &row);
        for (int i = 0; i < right_count; i++) {
            if (row[i / 8] & (0x80 >> (i % 8))) {
                result[static_cast<std::size_t>(i) + 1] = 1;
            }
        }
    }
    return result;
}

static connection_table dictionary_get_connect_matrix(dictionary_work& d)
{
    uint16_t left_count = 0;
    uint16_t right_count = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return {};
    }
    njd_r_get_connection_counts(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &left_count, &right_count);
    connection_table result;
    result.right_count = static_cast<std::size_t>(right_count) + 1;
    result.allowed.reserve((static_cast<std::size_t>(left_count) + 1) * result.right_count);
    for (int i = 0; i < left_count + 1; i++) {
        auto row = dictionary_get_connect_array(d, i);
        result.allowed.insert(result.allowed.end(), row.begin(), row.end());
    }
    return result;
}

static connector dictionary_get_connector(dictionary_work& d, connector_type type)
{
    uint8_t nj_type = 0;
    connector result;
    switch (type) {
    case connector_type_v1:
        nj_type = NJ_CONNECTION_V1_LEFT;
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return result;
    case connector_type_v2:
        nj_type = NJ_CONNECTION_V2_LEFT;
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return result;
    case connector_type_v3:
        nj_type = NJ_CONNECTION_V3_LEFT;
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return result;
    case connector_type_sentence_start:
        nj_type = NJ_CONNECTION_BUNTOU_RIGHT;
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return result;
    case connector_type_single_kanji:
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SINGLE_KANJI_LEFT);
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SINGLE_KANJI_RIGHT);
        return result;
    case connector_type_number:
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_NUMBER_RIGHT);
        return result;
    case connector_type_noun:
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_NOUN_LEFT);
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_NOUN_RIGHT);
        return result;
    case connector_type_person_name:
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PERSON_NAME_LEFT);
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PERSON_NAME_RIGHT);
        return result;
    case connector_type_place_name:
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PLACE_NAME_LEFT);
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PLACE_NAME_RIGHT);
        return result;
    case connector_type_symbol:
        result.left_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SYMBOL_LEFT);
        result.right_id = njd_r_get_connection_id(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SYMBOL_RIGHT);
        return result;
    }
    return result;
}

static word make_clause_word(const std::string& stroke, const word& stem)
{
    word out = stem;
    out.stroke = stroke;
    return out;
}

static word make_clause_word(const std::string& stroke, const word& stem, const word& fzk)
{
    word out;
    out.id = stem.id;
    out.candidate = stem.candidate + fzk.candidate;
    out.stroke = stroke;
    out.connection = { stem.connection.left_id, fzk.connection.right_id };
    out.frequency = stem.frequency;
    out.attribute = 1;
    return out;
}

static bool connectible(const clause_converter& c, int right, int left)
{
    return left >= 0 && right >= 0
        && c.connect_matrix.right_count != 0
        && static_cast<std::size_t>(right) < c.connect_matrix.right_count
        && (static_cast<std::size_t>(left) * c.connect_matrix.right_count
            + static_cast<std::size_t>(right)) < c.connect_matrix.allowed.size()
        && c.connect_matrix.allowed[static_cast<std::size_t>(left) * c.connect_matrix.right_count
                                    + static_cast<std::size_t>(right)] != 0;
}

static const std::vector<word>& get_independent_words(clause_converter& c, const std::string& input, bool all);
static const std::vector<word>& get_ancillary_pattern(clause_converter& c, const std::string& input);
static word default_clause_word(const clause_converter& c, const std::string& input);

static bool same_clause_word(const word& lhs, const word& rhs)
{
    return lhs.candidate == rhs.candidate
        && lhs.stroke == rhs.stroke
        && lhs.connection.left_id == rhs.connection.left_id
        && lhs.connection.right_id == rhs.connection.right_id;
}

static bool insert_clause(std::vector<clause>& clause_list, const clause& cl, std::size_t limit)
{
    auto same = std::find_if(clause_list.begin(), clause_list.end(), [&](const clause& x) {
        return same_clause_word(x.value, cl.value);
    });
    if (same != clause_list.end()) {
        if (same->value.frequency >= cl.value.frequency) {
            return false;
        }
        clause_list.erase(same);
    }

    auto it = std::find_if(clause_list.begin(), clause_list.end(), [&](const clause& x) {
        return x.value.frequency < cl.value.frequency;
    });
    clause_list.insert(it, cl);
    if (limit != 0 && clause_list.size() > limit) {
        bool kept = false;
        for (const auto& x : clause_list) {
            if (same_clause_word(x.value, cl.value)) {
                kept = true;
                break;
            }
        }
        clause_list.resize(limit);
        return kept && std::any_of(clause_list.begin(), clause_list.end(), [&](const clause& x) {
            return same_clause_word(x.value, cl.value);
        });
    }
    return true;
}

static bool add_clause(clause_converter& c, std::vector<clause>& clause_list, const std::string& input,
                       const word& stem, const word* fzk, const connector& terminal, bool all)
{
    std::optional<word> w;
    if (fzk == nullptr) {
        if (connectible(c, stem.connection.right_id, terminal.left_id)) {
            w = make_clause_word(input, stem);
        }
    } else if (connectible(c, stem.connection.right_id, fzk->connection.left_id)
               && connectible(c, fzk->connection.right_id, terminal.left_id)) {
        w = make_clause_word(input, stem, *fzk);
    }

    if (!w) {
        return false;
    }

    clause cl{ *w };
    return insert_clause(clause_list, cl, all ? 0 : 1);
}

static bool single_clause_convert(clause_converter& c, std::vector<clause>& clause_list,
                                  const std::string& input, const connector& terminal, bool all)
{
    bool ret = false;
    const auto& direct_stems = get_independent_words(c, input, all);
    for (const auto& stem : direct_stems) {
        if (add_clause(c, clause_list, input, stem, nullptr, terminal, all)) {
            ret = true;
        }
    }

    const utf8_input_view input_view(input);
    const std::size_t input_len = input_view.size();
    for (std::size_t split = 1; split < input_len; split++) {
        const auto& fzks = get_ancillary_pattern(c, input_view.span(split, input_len));
        if (fzks.empty()) {
            continue;
        }

        const std::string stem_key = input_view.span(0, split);
        const auto& stems = get_independent_words(c, stem_key, all);
        if (stems.empty()) {
            if (dictionary_search(*c.dictionary, search_prefix, order_by_frequency, stem_key) <= 0) {
                break;
            }
            continue;
        }

        for (const auto& stem : stems) {
            for (const auto& fzk : fzks) {
                if (add_clause(c, clause_list, input, stem, &fzk, terminal, all)) {
                    ret = true;
                }
            }
        }
    }
    return ret;
}

static const std::vector<word>& get_ancillary_pattern(clause_converter& c, const std::string& input)
{
    if (auto it = c.fzk_patterns.find(input); it != c.fzk_patterns.end()) {
        return it->second;
    }
    if (c.fzk_patterns.size() >= max_cache_entries) {
        c.fzk_patterns.clear();
    }
    if (input.empty()) {
        return c.fzk_patterns.try_emplace(input).first->second;
    }

    auto& dict = *c.dictionary;
    dictionary_clear(dict);
    dictionary_clear_approx(dict);
    dictionary_set(dict, dictionary_ancillary, 400, 500);

    const utf8_input_view input_view(input);
    const std::size_t input_len = input_view.size();
    for (std::size_t start_rev = input_len; start_rev > 0; --start_rev) {
        std::size_t start = start_rev - 1;
        std::string key = input_view.span(start, input_len);
        if (c.fzk_patterns.contains(key)) {
            continue;
        }

        std::vector<word> fzks;
        dictionary_search(dict, search_exact, order_by_frequency, key);
        while (auto w = dictionary_get_next(dict)) {
            fzks.push_back(*w);
        }

        for (std::size_t end = input_len - 1; end > start; --end) {
            std::string follow_key = input_view.span(end, input_len);
            auto follow_it = c.fzk_patterns.find(follow_key);
            if (follow_it == c.fzk_patterns.end() || follow_it->second.empty()) {
                continue;
            }
            dictionary_search(dict, search_exact, order_by_frequency, input_view.span(start, end));
            while (auto w = dictionary_get_next(dict)) {
                for (const auto& follow : follow_it->second) {
                    if (connectible(c, w->connection.right_id, follow.connection.left_id)) {
                        word combined;
                        combined.candidate = w->candidate + follow.candidate;
                        combined.stroke = key;
                        combined.connection = { w->connection.left_id, follow.connection.right_id };
                        fzks.push_back(combined);
                    }
                }
            }
        }

        c.fzk_patterns.insert_or_assign(key, std::move(fzks));
    }
    return c.fzk_patterns.find(input)->second;
}

static void merge_word(std::vector<word>& words, const word& value)
{
    auto same = std::find_if(words.begin(), words.end(), [&](const word& known) {
        return same_clause_word(known, value);
    });
    if (same == words.end()) {
        words.push_back(value);
    } else if (same->frequency < value.frequency) {
        *same = value;
    }
}

static const std::vector<word>& get_independent_words(clause_converter& c, const std::string& input, bool all)
{
    auto& bag = all ? c.all_indep_word_bag : c.indep_word_bag;
    if (auto it = bag.find(input); it != bag.end()) {
        return it->second;
    }
    if (bag.size() >= max_cache_entries) {
        bag.clear();
    }
    if (input.empty()) {
        return bag.try_emplace(input).first->second;
    }

    std::vector<word> words;
    auto& dict = *c.dictionary;
    dictionary_clear(dict);
    dictionary_clear_approx(dict);
    dictionary_set(dict, dictionary_single_kanji, 0, 10);
    dictionary_set(dict, dictionary_independent, 400, 500);

    dictionary_search(dict, search_exact, order_by_frequency, input);
    if (all) {
        while (auto w = dictionary_get_next(dict)) {
            if (w->stroke == input) {
                merge_word(words, *w);
            }
        }
    } else {
        while (auto w = dictionary_get_next(dict)) {
            if (w->stroke == input) {
                auto known = std::find_if(words.begin(), words.end(), [&](const word& value) {
                    return value.connection.right_id == w->connection.right_id;
                });
                if (known == words.end()) {
                    merge_word(words, *w);
                } else if (known->frequency < w->frequency) {
                    *known = *w;
                }
            }
        }
    }

    const auto merge_dynamic_words = [&](const std::vector<word>* dynamic_words) {
        if (dynamic_words == nullptr) {
            return;
        }
        for (const auto& entry : *dynamic_words) {
            if (entry.stroke == input) {
                merge_word(words, entry);
            }
        }
    };
    merge_dynamic_words(c.user_words);
    merge_dynamic_words(c.learned_words);

    // OpenWnn injects its autogenerated raw candidate at independent-word
    // lookup time so it participates in clauses, ancillary expansion, and DP.
    merge_word(words, default_clause_word(c, input));
    std::stable_sort(words.begin(), words.end(), [](const word& lhs, const word& rhs) {
        return lhs.frequency > rhs.frequency;
    });

    return bag.emplace(input, std::move(words)).first->second;
}

static word default_clause_word(const clause_converter& c, const std::string& input)
{
    word w;
    w.candidate = input;
    w.stroke = input;
    w.connection = c.default_connector;
    w.frequency = (clause_cost - 1) * static_cast<int>(utf8_codepoint_count(input));
    return w;
}

struct sentence_node {
    clause element;
    int frequency = std::numeric_limits<int>::min();
    int previous_end = -1;
};

static bool span_is_allowed(std::size_t begin, std::size_t end,
                            const std::optional<std::pair<std::size_t, std::size_t>>& forced)
{
    if (!forced) {
        return true;
    }
    const auto [forced_begin, forced_end] = *forced;
    if (end <= forced_begin || begin >= forced_end) {
        return true;
    }
    return begin == forced_begin && end == forced_end;
}

static std::optional<sentence> consecutive_clause_convert(
    clause_converter& c, const std::string& input,
    const std::optional<std::pair<std::size_t, std::size_t>>& forced = std::nullopt)
{
    const utf8_input_view input_view(input);
    const std::size_t input_len = input_view.size();
    if (input_len == 0) {
        return std::nullopt;
    }

    std::vector<std::optional<sentence_node>> nodes(input_len);
    for (std::size_t start = 0; start < input_len; start++) {
        if (start != 0 && !nodes[start - 1]) {
            continue;
        }

        std::size_t end = std::min(input_len, start + 20);
        for (; end > start; --end) {
            if (!span_is_allowed(start, end, forced)) {
                continue;
            }
            std::size_t idx = end - 1;
            const int base = start == 0 ? 0 : nodes[start - 1]->frequency;
            if (nodes[idx]) {
                if (nodes[idx]->frequency > base + clause_cost + freq_learn) {
                    break;
                }
            }

            std::string key = input_view.span(start, end);
            std::vector<clause> clauses;
            if (end == input_len) {
                single_clause_convert(c, clauses, key, c.end_clause_connector_2, false);
            } else {
                single_clause_convert(c, clauses, key, c.end_clause_connector_3, false);
            }
            if (clauses.empty()) {
                clauses.push_back(clause{ default_clause_word(c, key) });
            }

            const clause& best = clauses.front();
            const int frequency = base + best.value.frequency + clause_cost;
            if (!nodes[idx] || frequency > nodes[idx]->frequency) {
                nodes[idx] = sentence_node{ best, frequency,
                    start == 0 ? -1 : static_cast<int>(start - 1) };
            }
        }
    }

    if (!nodes[input_len - 1]) {
        return std::nullopt;
    }

    sentence result;
    result.value.frequency = nodes[input_len - 1]->frequency;
    for (int index = static_cast<int>(input_len - 1); index >= 0;) {
        const sentence_node& node = *nodes[static_cast<std::size_t>(index)];
        result.elements.push_back(node.element);
        index = node.previous_end;
    }
    std::reverse(result.elements.begin(), result.elements.end());
    result.value.stroke = input;
    result.value.connection.left_id = result.elements.front().value.connection.left_id;
    result.value.connection.right_id = result.elements.back().value.connection.right_id;
    result.value.attribute = result.elements.size() > 1 ? 2 : result.elements.front().value.attribute;
    for (const auto& element : result.elements) {
        result.value.candidate += element.value.candidate;
    }
    return result;
}

static void clear_clause_caches(clause_converter& c)
{
    c.indep_word_bag.clear();
    c.all_indep_word_bag.clear();
    c.fzk_patterns.clear();
}

static void synchronize_clause_caches(clause_converter& c, std::uint64_t dictionary_version)
{
    if (c.cache_version != dictionary_version) {
        clear_clause_caches(c);
        c.cache_version = dictionary_version;
    }
}

static void clause_converter_init(clause_converter& c, dictionary_work& dict)
{
    c.dictionary = &dict;
    c.connect_matrix = dictionary_get_connect_matrix(dict);
    clear_clause_caches(c);
    c.default_connector = dictionary_get_connector(dict, connector_type_noun);
    c.end_clause_connector_1 = dictionary_get_connector(dict, connector_type_v1);
    c.end_clause_connector_2 = dictionary_get_connector(dict, connector_type_v2);
    c.end_clause_connector_3 = dictionary_get_connector(dict, connector_type_v3);
}

static std::vector<candidate> words_to_candidates(const std::vector<word>& words, std::size_t limit)
{
    std::vector<candidate> out;
    out.reserve(std::min(words.size(), limit));
    std::unordered_set<std::string> seen;
    for (const auto& w : words) {
        if (w.candidate.empty() || utf8_codepoint_count(w.candidate) > max_output_length) {
            continue;
        }
        if (!seen.insert(w.candidate).second) {
            continue;
        }
        out.push_back({ w.candidate, w.stroke, w.frequency, w.connection, w.attribute });
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

static void set_dictionary_for_prediction(dictionary_work& dict, std::size_t input_len)
{
    dictionary_clear(dict);
    dictionary_clear_approx(dict);
    if (input_len == 0) {
        dictionary_set(dict, dictionary_suffix, 100, 245);
    } else {
        dictionary_set(dict, dictionary_prediction, 100, 400);
        dictionary_set(dict, dictionary_suffix, 100, 245);
    }
}

static const std::unordered_map<std::string, std::string>& romaji_table()
{
    static const std::unordered_map<std::string, std::string> table = {
        {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
        {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
        {"sa", "さ"}, {"shi", "し"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
        {"ta", "た"}, {"chi", "ち"}, {"ti", "ち"}, {"tsu", "つ"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
        {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
        {"ha", "は"}, {"hi", "ひ"}, {"fu", "ふ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
        {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
        {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
        {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
        {"wa", "わ"}, {"wo", "を"}, {"nn", "ん"}, {"n", "ん"},
        {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
        {"za", "ざ"}, {"ji", "じ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
        {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
        {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
        {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
        {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
        {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
        {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
        {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
        {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
        {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
        {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
        {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
        {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"}, {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
        {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
        {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
        {"la", "ぁ"}, {"li", "ぃ"}, {"lu", "ぅ"}, {"le", "ぇ"}, {"lo", "ぉ"},
        {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
        {"ltu", "っ"}, {"xtu", "っ"}, {"lya", "ゃ"}, {"lyu", "ゅ"}, {"lyo", "ょ"},
        {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"}, {"-", "ー"}
    };
    return table;
}

} // namespace

struct engine {
    dictionary_work dictionary;
    clause_converter converter;
    std::vector<word> user_words;
    std::vector<word> learned_words;
    std::uint64_t dictionary_version = 1;
};

void engine_deleter::operator()(engine* e) const noexcept
{
    delete e;
}

engine_ptr engine_create()
{
    engine_ptr e(new engine());
    dictionary_init(e->dictionary);
    dictionary_clear(e->dictionary);
    dictionary_clear_approx(e->dictionary);
    clause_converter_init(e->converter, e->dictionary);
    e->converter.user_words = &e->user_words;
    e->converter.learned_words = &e->learned_words;
    e->converter.cache_version = e->dictionary_version;
    return e;
}

void engine_reset(engine& e)
{
    e.user_words.clear();
    e.learned_words.clear();
    ++e.dictionary_version;
    dictionary_init(e.dictionary);
    dictionary_clear(e.dictionary);
    dictionary_clear_approx(e.dictionary);
    clause_converter_init(e.converter, e.dictionary);
    e.converter.user_words = &e.user_words;
    e.converter.learned_words = &e.learned_words;
    e.converter.cache_version = e.dictionary_version;
}

std::vector<candidate> engine_predict(engine& e, const std::string& utf8_hiragana, std::size_t limit)
{
    auto input_len = validated_utf8_code_unit_count(utf8_hiragana);
    if (!input_len || *input_len == 0 || limit == 0) {
        return {};
    }
    set_dictionary_for_prediction(e.dictionary, *input_len);
    dictionary_search(e.dictionary, search_prefix, order_by_frequency, utf8_hiragana);

    std::vector<word> words;
    std::unordered_set<std::string> seen;
    while (auto w = dictionary_get_next(e.dictionary)) {
        if (w->candidate.empty()
            || utf8_codepoint_count(w->candidate) > max_output_length
            || !seen.insert(w->candidate).second) {
            continue;
        }
        words.push_back(*w);
        if (words.size() >= limit) {
            break;
        }
    }
    const auto append_dynamic = [&](const std::vector<word>& dynamic_words) {
        for (const auto& value : dynamic_words) {
            if (value.stroke.starts_with(utf8_hiragana)) {
                words.push_back(value);
            }
        }
    };
    append_dynamic(e.user_words);
    append_dynamic(e.learned_words);
    std::stable_sort(words.begin(), words.end(), [](const word& lhs, const word& rhs) {
        return lhs.frequency > rhs.frequency;
    });
    return words_to_candidates(words, limit);
}

std::optional<sentence> engine_convert_best(engine& e, const std::string& utf8_hiragana)
{
    auto input_len = validated_utf8_code_unit_count(utf8_hiragana);
    if (!input_len || *input_len == 0 || *input_len > max_input_length) {
        return std::nullopt;
    }
    synchronize_clause_caches(e.converter, e.dictionary_version);
    return consecutive_clause_convert(e.converter, utf8_hiragana);
}

std::vector<candidate> engine_get_clause_candidates(engine& e,
                                                    const std::string& utf8_hiragana,
                                                    std::size_t clause_position,
                                                    std::size_t clause_length,
                                                    std::size_t limit)
{
    auto input_len = validated_utf8_code_unit_count(utf8_hiragana);
    const utf8_input_view input_view(utf8_hiragana);
    if (!input_len || *input_len == 0 || *input_len > max_input_length || limit == 0
        || clause_length == 0 || clause_position > input_view.size()
        || clause_length > input_view.size() - clause_position) {
        return {};
    }
    synchronize_clause_caches(e.converter, e.dictionary_version);
    const std::string selected = input_view.span(clause_position, clause_position + clause_length);
    std::vector<clause> clauses;
    single_clause_convert(e.converter, clauses, selected, e.converter.end_clause_connector_2, true);
    std::vector<word> words;
    words.reserve(clauses.size());
    for (const auto& value : clauses) {
        words.push_back(value.value);
    }
    return words_to_candidates(words, limit);
}

std::optional<sentence> engine_resize_clause(engine& e,
                                             const std::string& utf8_hiragana,
                                             std::size_t clause_position,
                                             std::size_t clause_length)
{
    auto input_len = validated_utf8_code_unit_count(utf8_hiragana);
    const utf8_input_view input_view(utf8_hiragana);
    if (!input_len || *input_len == 0 || *input_len > max_input_length
        || clause_length == 0 || clause_position > input_view.size()
        || clause_length > input_view.size() - clause_position) {
        return std::nullopt;
    }
    synchronize_clause_caches(e.converter, e.dictionary_version);
    return consecutive_clause_convert(e.converter, utf8_hiragana,
        std::pair{ clause_position, clause_position + clause_length });
}

std::vector<candidate> engine_convert(engine& e, const std::string& utf8_hiragana, std::size_t limit)
{
    if (limit == 0) {
        return {};
    }
    auto best = engine_convert_best(e, utf8_hiragana);
    if (!best) {
        return {};
    }
    return { candidate{ best->value.candidate, best->value.stroke, best->value.frequency,
                        best->value.connection, best->value.attribute } };
}

static bool valid_dynamic_word(const word& value)
{
    auto stroke_len = validated_utf8_code_unit_count(value.stroke);
    auto candidate_len = validated_utf8_code_unit_count(value.candidate);
    return stroke_len && candidate_len && *stroke_len > 0 && *candidate_len > 0
        && *stroke_len <= max_input_length && *candidate_len <= max_output_length
        && value.connection.left_id >= 0 && value.connection.right_id >= 0;
}

bool engine_add_user_word(engine& e, const word& entry)
{
    if (!valid_dynamic_word(entry)) {
        return false;
    }
    word value = entry;
    if (value.frequency == 0) {
        value.frequency = freq_user;
    }
    merge_word(e.user_words, value);
    ++e.dictionary_version;
    return true;
}

bool engine_remove_user_word(engine& e, const word& entry)
{
    auto it = std::find_if(e.user_words.begin(), e.user_words.end(), [&](const word& value) {
        return same_clause_word(value, entry);
    });
    if (it == e.user_words.end()) {
        return false;
    }
    e.user_words.erase(it);
    ++e.dictionary_version;
    return true;
}

void engine_clear_user_words(engine& e)
{
    if (!e.user_words.empty()) {
        e.user_words.clear();
        ++e.dictionary_version;
    }
}

bool engine_learn_candidate(engine& e, const candidate& selected, int frequency_delta)
{
    word value;
    value.candidate = selected.candidate;
    value.stroke = selected.stroke;
    value.frequency = selected.frequency;
    value.connection = selected.connection;
    value.attribute = selected.attribute;
    if (frequency_delta <= 0 || !valid_dynamic_word(value)) {
        return false;
    }
    auto it = std::find_if(e.learned_words.begin(), e.learned_words.end(), [&](const word& known) {
        return same_clause_word(known, value);
    });
    if (it == e.learned_words.end()) {
        value.frequency = std::max(freq_learn, value.frequency) + frequency_delta;
        e.learned_words.push_back(std::move(value));
    } else {
        it->frequency += frequency_delta;
    }
    ++e.dictionary_version;
    return true;
}

void engine_clear_learning(engine& e)
{
    if (!e.learned_words.empty()) {
        e.learned_words.clear();
        ++e.dictionary_version;
    }
}

std::string romaji_to_hiragana(const std::string& ascii_romaji)
{
    std::string input = to_ascii_lower(ascii_romaji);
    std::string out;
    for (std::size_t i = 0; i < input.size();) {
        if (i + 1 < input.size()
            && input[i] == input[i + 1]
            && std::string("bcdfghjklmpqrstvwxyz").find(input[i]) != std::string::npos
            && input[i] != 'n') {
            out += "っ";
            i++;
            continue;
        }

        bool matched = false;
        for (std::size_t len = 3; len >= 1; --len) {
            if (i + len > input.size()) {
                continue;
            }
            std::string key = input.substr(i, len);
            auto it = romaji_table().find(key);
            if (it != romaji_table().end()) {
                out += it->second;
                i += len;
                matched = true;
                break;
            }
            if (len == 1) {
                break;
            }
        }
        if (!matched) {
            out.push_back(input[i]);
            i++;
        }
    }
    return out;
}

} // namespace kanaria
