/*
 * Std C++23 porting layer for the Qt OpenWnn library.
 *
 * Copyright (C) 2015 The Qt Company
 * Copyright (C) 2008-2012 OMRON SOFTWARE Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "openwnn_std.h"

extern "C" {
#include "nj_lib.h"
#include "nj_err.h"
#include "nj_ext.h"
#include "nj_dic.h"

extern NJ_UINT32 dic_size[];
extern NJ_UINT8 dic_type[];
extern NJ_UINT8 *dic_data[];
extern NJ_UINT8 *con_data[];
}

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace openwnn {
namespace {

constexpr int nj_func_set_dictionary_parameters = 0x00FA;
constexpr int nj_func_search_word = 0x003C;
constexpr int nj_func_set_left_part_of_speech = 0x00F3;
constexpr int nj_func_set_right_part_of_speech = 0x00F2;
constexpr int nj_func_set_stroke = 0x00F1;
constexpr int nj_func_set_candidate = 0x00F0;
constexpr int nj_func_get_left_pos_by_type = 0x00EE;
constexpr int nj_func_get_right_pos_by_type = 0x00ED;

constexpr int nj_err_invalid_param = 0x7B00;

constexpr int flag_none = 0x00;
constexpr int flag_enable_cursor = 0x01;
constexpr int flag_enable_result = 0x02;

constexpr int freq_learn = 600;
constexpr int freq_user = 500;
constexpr int max_input_length = 50;
constexpr int max_output_length = 50;
constexpr int clause_cost = -1000;
constexpr std::size_t clause_beam_width = 8;
constexpr std::size_t sentence_beam_width = 8;
constexpr int stem_length_bonus = 24;
constexpr int fzk_length_penalty = 8;

enum search_operation {
    search_exact,
    search_prefix,
    search_link,
};

enum search_order {
    order_by_frequency,
    order_by_key,
};

enum pos_type {
    pos_type_v1,
    pos_type_v2,
    pos_type_v3,
    pos_type_buntou,
    pos_type_tankanji,
    pos_type_suuji,
    pos_type_meisi,
    pos_type_jinmei,
    pos_type_chimei,
    pos_type_kigou,
};

struct nj_work {
    NJ_DIC_HANDLE dic_handle[NJ_MAX_DIC];
    NJ_UINT32 dic_size[NJ_MAX_DIC];
    NJ_UINT8 dic_type[NJ_MAX_DIC];
    NJ_CHAR key_string[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_RESULT result;
    NJ_CURSOR cursor;
    NJ_SEARCH_CACHE search_cache[NJ_MAX_DIC];
    NJ_DIC_SET dic_set;
    NJ_CLASS wnn_class;
    NJ_CHARSET approx_set;
    NJ_CHAR previous_stroke[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_CHAR previous_candidate[NJ_MAX_RESULT_LEN + NJ_TERM_LEN];
    NJ_UINT8 flag;
};

struct dictionary_work {
    nj_work work{};
};

struct clause_converter {
    dictionary_work* dictionary = nullptr;
    std::map<std::string, std::vector<word>> indep_word_bag;
    std::map<std::string, std::vector<word>> all_indep_word_bag;
    std::map<std::string, std::vector<word>> fzk_patterns;
    std::vector<std::vector<unsigned char>> connect_matrix;
    pos pos_default;
    pos pos_end_clause_1;
    pos pos_end_clause_2;
    pos pos_end_clause_3;
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

static std::optional<std::size_t> validated_utf8_codepoint_count(const std::string& s)
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
        ++count;
    }
    return count;
}

static std::vector<std::size_t> utf8_offsets(const std::string& s)
{
    std::vector<std::size_t> offsets;
    offsets.reserve(s.size() + 1);
    offsets.push_back(0);
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t step = 1;
        if ((c & 0x80) == 0x00) {
            step = 1;
        } else if ((c & 0xE0) == 0xC0) {
            step = 2;
        } else if ((c & 0xF0) == 0xE0) {
            step = 3;
        } else if ((c & 0xF8) == 0xF0) {
            step = 4;
        }
        i = std::min(i + step, s.size());
        offsets.push_back(i);
    }
    return offsets;
}

static std::string utf8_mid(const std::string& s, std::size_t first, std::size_t len = static_cast<std::size_t>(-1))
{
    auto offsets = utf8_offsets(s);
    if (first >= offsets.size() - 1) {
        return {};
    }
    std::size_t last = len == static_cast<std::size_t>(-1)
        ? offsets.size() - 1
        : std::min(first + len, offsets.size() - 1);
    return s.substr(offsets[first], offsets[last] - offsets[first]);
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

static NJ_CHAR convert_utf16be_char_to_nj_char(const NJ_UINT8* src)
{
    NJ_CHAR ret;
    auto* dst = reinterpret_cast<NJ_UINT8*>(&ret);
    dst[0] = src[0];
    dst[1] = src[1];
    return ret;
}

static void convert_string_to_nj_char(NJ_CHAR* dst, const std::string& src_string,
                                      int max_chars, int capacity_chars)
{
    const auto* src = reinterpret_cast<const unsigned char*>(src_string.c_str());
    int i = 0;
    int o = 0;

    if (capacity_chars <= 0) {
        return;
    }

    const int max_output_chars = std::min(max_chars, capacity_chars - 1);
    while (src[i] != 0x00 && o < max_output_chars) {
        auto* dst_tmp = reinterpret_cast<NJ_UINT8*>(&dst[o]);

        if ((src[i] & 0x80) == 0x00) {
            dst_tmp[0] = 0x00;
            dst_tmp[1] = src[i] & 0x7f;
            i++;
            o++;
        } else if ((src[i] & 0xe0) == 0xc0) {
            if (src[i + 1] == 0x00) {
                break;
            }
            dst_tmp[0] = ((src[i] & 0x1f) >> 2);
            dst_tmp[1] = ((src[i] & 0x1f) << 6) | (src[i + 1] & 0x3f);
            i += 2;
            o++;
        } else if ((src[i] & 0xf0) == 0xe0) {
            if (src[i + 1] == 0x00 || src[i + 2] == 0x00) {
                break;
            }
            dst_tmp[0] = ((src[i] & 0x0f) << 4) | ((src[i + 1] & 0x3f) >> 2);
            dst_tmp[1] = ((src[i + 1] & 0x3f) << 6) | (src[i + 2] & 0x3f);
            i += 3;
            o++;
        } else if ((src[i] & 0xf8) == 0xf0) {
            if (!(o < max_output_chars - 1)) {
                break;
            }
            if (src[i + 1] == 0x00 || src[i + 2] == 0x00 || src[i + 3] == 0x00) {
                break;
            }
            NJ_UINT8 dst1 = static_cast<NJ_UINT8>((((src[i] & 0x07) << 2) | ((src[i + 1] & 0x3f) >> 4)) - 1);
            NJ_UINT8 dst2 = static_cast<NJ_UINT8>(((src[i + 1] & 0x3f) << 4) | ((src[i + 2] & 0x3f) >> 2));
            NJ_UINT8 dst3 = static_cast<NJ_UINT8>(((src[i + 2] & 0x3f) << 6) | (src[i + 3] & 0x3f));
            dst_tmp[0] = static_cast<NJ_UINT8>(0xd8 | ((dst1 & 0x0c) >> 2));
            dst_tmp[1] = static_cast<NJ_UINT8>(((dst1 & 0x03) << 6) | ((dst2 & 0xfc) >> 2));
            dst_tmp[2] = static_cast<NJ_UINT8>(0xdc | (dst2 & 0x03));
            dst_tmp[3] = dst3;
            i += 4;
            o += 2;
        } else {
            break;
        }
    }

    dst[o] = NJ_CHAR_NUL;
}

static std::string convert_nj_char_to_string(const NJ_CHAR* src, int max_chars)
{
    std::string dst;
    dst.resize((NJ_MAX_LEN + NJ_MAX_RESULT_LEN + NJ_TERM_LEN) * 4 + 1);
    int i = 0;
    int o = 0;

    while (src[i] != 0x0000 && i < max_chars) {
        auto* src_tmp = reinterpret_cast<const NJ_UINT8*>(&src[i]);
        if (src_tmp[0] == 0x00 && src_tmp[1] <= 0x7f) {
            dst[o++] = static_cast<char>(src_tmp[1] & 0x7f);
            i++;
        } else if (src_tmp[0] <= 0x07) {
            dst[o++] = static_cast<char>(0xc0 | ((src_tmp[0] & 0x07) << 2) | ((src_tmp[1] & 0xc0) >> 6));
            dst[o++] = static_cast<char>(0x80 | (src_tmp[1] & 0x3f));
            i++;
        } else if (src_tmp[0] >= 0xd8 && src_tmp[0] <= 0xdb) {
            if (!(i < max_chars - 1) || src_tmp[2] < 0xdc || src_tmp[2] > 0xdf) {
                break;
            }
            NJ_UINT8 src1 = static_cast<NJ_UINT8>((((src_tmp[0] & 0x03) << 2) | ((src_tmp[1] & 0xc0) >> 6)) + 1);
            NJ_UINT8 src2 = static_cast<NJ_UINT8>(((src_tmp[1] & 0x3f) << 2) | (src_tmp[2] & 0x03));
            NJ_UINT8 src3 = src_tmp[3];
            dst[o++] = static_cast<char>(0xf0 | ((src1 & 0x1c) >> 2));
            dst[o++] = static_cast<char>(0x80 | ((src1 & 0x03) << 4) | ((src2 & 0xf0) >> 4));
            dst[o++] = static_cast<char>(0x80 | ((src2 & 0x0f) << 2) | ((src3 & 0xc0) >> 6));
            dst[o++] = static_cast<char>(0x80 | (src3 & 0x3f));
            i += 2;
        } else {
            dst[o++] = static_cast<char>(0xe0 | ((src_tmp[0] & 0xf0) >> 4));
            dst[o++] = static_cast<char>(0x80 | ((src_tmp[0] & 0x0f) << 2) | ((src_tmp[1] & 0xc0) >> 6));
            dst[o++] = static_cast<char>(0x80 | (src_tmp[1] & 0x3f));
            i++;
        }
    }

    dst.resize(static_cast<std::size_t>(o));
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
    out.part_of_speech.left = NJ_GET_FPOS_FROM_STEM(&d.work.result.word);
    out.part_of_speech.right = NJ_GET_BPOS_FROM_STEM(&d.work.result.word);
    return out;
}

static int dictionary_set_left_pos(dictionary_work& d, int left)
{
    NJ_UINT16 lcount = 0;
    NJ_UINT16 rcount = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return NJ_SET_ERR_VAL(nj_func_set_left_part_of_speech, NJ_ERR_NO_RULEDIC);
    }
    njd_r_get_count(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &lcount, &rcount);
    if (left < 1 || left > lcount) {
        return NJ_SET_ERR_VAL(nj_func_set_left_part_of_speech, nj_err_invalid_param);
    }
    NJ_SET_FPOS_TO_STEM(&d.work.result.word, left);
    return 0;
}

static int dictionary_set_right_pos(dictionary_work& d, int right)
{
    NJ_UINT16 lcount = 0;
    NJ_UINT16 rcount = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return NJ_SET_ERR_VAL(nj_func_set_right_part_of_speech, NJ_ERR_NO_RULEDIC);
    }
    njd_r_get_count(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &lcount, &rcount);
    if (right < 1 || right > rcount) {
        return NJ_SET_ERR_VAL(nj_func_set_right_part_of_speech, nj_err_invalid_param);
    }
    NJ_SET_BPOS_TO_STEM(&d.work.result.word, right);
    return 0;
}

static int dictionary_set_stroke(dictionary_work& d, const std::string& stroke)
{
    if (stroke.empty()) {
        return NJ_SET_ERR_VAL(nj_func_set_stroke, nj_err_invalid_param);
    }
    convert_string_to_nj_char(d.work.previous_stroke, stroke, NJ_MAX_LEN, NJ_MAX_LEN + NJ_TERM_LEN);
    return 0;
}

static int dictionary_set_candidate(dictionary_work& d, const std::string& candidate_text)
{
    if (candidate_text.empty()) {
        return NJ_SET_ERR_VAL(nj_func_set_candidate, nj_err_invalid_param);
    }
    convert_string_to_nj_char(d.work.previous_candidate, candidate_text,
                              NJ_MAX_RESULT_LEN, NJ_MAX_RESULT_LEN + NJ_TERM_LEN);
    return 0;
}

static int dictionary_select_word(dictionary_work& d)
{
    std::memcpy(&d.work.wnn_class.dic_set, &d.work.dic_set, sizeof(NJ_DIC_SET));
    return njx_select(&d.work.wnn_class, &d.work.result);
}

static std::vector<unsigned char> dictionary_get_connect_array(dictionary_work& d, int left_pos)
{
    NJ_UINT16 lcount = 0;
    NJ_UINT16 rcount = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return {};
    }
    njd_r_get_count(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &lcount, &rcount);
    if (left_pos < 0 || left_pos > lcount) {
        return {};
    }

    std::vector<unsigned char> result(static_cast<std::size_t>(rcount) + 1, 0);
    NJ_UINT8* connect = nullptr;
    if (left_pos > 0) {
        njd_r_get_connect(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], left_pos, NJ_RULE_TYPE_FTOB, &connect);
        for (int i = 0; i < rcount; i++) {
            if (connect[i / 8] & (0x80 >> (i % 8))) {
                result[static_cast<std::size_t>(i) + 1] = 1;
            }
        }
    }
    return result;
}

static std::vector<std::vector<unsigned char>> dictionary_get_connect_matrix(dictionary_work& d)
{
    NJ_UINT16 lcount = 0;
    NJ_UINT16 rcount = 0;
    if (d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN] == nullptr) {
        return {};
    }
    njd_r_get_count(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], &lcount, &rcount);
    std::vector<std::vector<unsigned char>> result;
    result.reserve(static_cast<std::size_t>(lcount) + 1);
    for (int i = 0; i < lcount + 1; i++) {
        result.push_back(dictionary_get_connect_array(d, i));
    }
    return result;
}

static pos dictionary_get_pos(dictionary_work& d, pos_type type)
{
    NJ_UINT8 nj_type = 0;
    pos p;
    switch (type) {
    case pos_type_v1:
        nj_type = NJ_HINSI_V1_F;
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return p;
    case pos_type_v2:
        nj_type = NJ_HINSI_V2_F;
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return p;
    case pos_type_v3:
        nj_type = NJ_HINSI_V3_F;
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return p;
    case pos_type_buntou:
        nj_type = NJ_HINSI_BUNTOU_B;
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], nj_type);
        return p;
    case pos_type_tankanji:
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_TANKANJI_F);
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_TANKANJI_B);
        return p;
    case pos_type_suuji:
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_SUUJI_B);
        return p;
    case pos_type_meisi:
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_MEISI_F);
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_MEISI_B);
        return p;
    case pos_type_jinmei:
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_JINMEI_F);
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_JINMEI_B);
        return p;
    case pos_type_chimei:
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_CHIMEI_F);
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_CHIMEI_B);
        return p;
    case pos_type_kigou:
        p.left = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_KIGOU_F);
        p.right = njd_r_get_hinsi(d.work.dic_set.rHandle[NJ_MODE_TYPE_HENKAN], NJ_HINSI_KIGOU_B);
        return p;
    }
    return p;
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
    out.part_of_speech = { stem.part_of_speech.left, fzk.part_of_speech.right };
    out.frequency = stem.frequency
        + stem_length_bonus * static_cast<int>(utf8_codepoint_count(stem.stroke))
        - fzk_length_penalty * static_cast<int>(utf8_codepoint_count(fzk.stroke));
    out.attribute = 1;
    return out;
}

static bool connectible(const clause_converter& c, int right, int left)
{
    return left >= 0 && right >= 0
        && static_cast<std::size_t>(left) < c.connect_matrix.size()
        && static_cast<std::size_t>(right) < c.connect_matrix[static_cast<std::size_t>(left)].size()
        && c.connect_matrix[static_cast<std::size_t>(left)][static_cast<std::size_t>(right)] != 0;
}

static std::vector<word> get_independent_words(clause_converter& c, const std::string& input, bool all);
static std::vector<word> get_ancillary_pattern(clause_converter& c, const std::string& input);

static bool same_clause_word(const word& lhs, const word& rhs)
{
    return lhs.candidate == rhs.candidate
        && lhs.stroke == rhs.stroke
        && lhs.part_of_speech.left == rhs.part_of_speech.left
        && lhs.part_of_speech.right == rhs.part_of_speech.right;
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
                       const word& stem, const word* fzk, const pos& terminal, bool all)
{
    std::optional<word> w;
    if (fzk == nullptr) {
        if (connectible(c, stem.part_of_speech.right, terminal.left)) {
            w = make_clause_word(input, stem);
        }
    } else if (connectible(c, stem.part_of_speech.right, fzk->part_of_speech.left)
               && connectible(c, fzk->part_of_speech.right, terminal.left)) {
        w = make_clause_word(input, stem, *fzk);
    }

    if (!w) {
        return false;
    }

    clause cl{ *w };
    return insert_clause(clause_list, cl, all ? 0 : clause_beam_width);
}

static bool single_clause_convert(clause_converter& c, std::vector<clause>& clause_list,
                                  const std::string& input, const pos& terminal, bool all)
{
    bool ret = false;
    auto stems = get_independent_words(c, input, all);
    for (const auto& stem : stems) {
        if (add_clause(c, clause_list, input, stem, nullptr, terminal, all)) {
            ret = true;
        }
    }

    const std::size_t input_len = utf8_codepoint_count(input);
    for (std::size_t split = 1; split < input_len; split++) {
        auto fzks = get_ancillary_pattern(c, utf8_mid(input, split));
        if (fzks.empty()) {
            continue;
        }

        const std::string stem_key = utf8_mid(input, 0, split);
        stems = get_independent_words(c, stem_key, all);
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

static std::vector<word> get_ancillary_pattern(clause_converter& c, const std::string& input)
{
    if (input.empty()) {
        return {};
    }
    if (auto it = c.fzk_patterns.find(input); it != c.fzk_patterns.end()) {
        return it->second;
    }

    auto& dict = *c.dictionary;
    dictionary_clear(dict);
    dictionary_clear_approx(dict);
    dictionary_set(dict, 6, 400, 500);

    const std::size_t input_len = utf8_codepoint_count(input);
    for (std::size_t start_rev = input_len; start_rev > 0; --start_rev) {
        std::size_t start = start_rev - 1;
        std::string key = utf8_mid(input, start);
        if (c.fzk_patterns.contains(key)) {
            continue;
        }

        std::vector<word> fzks;
        dictionary_search(dict, search_exact, order_by_frequency, key);
        while (auto w = dictionary_get_next(dict)) {
            fzks.push_back(*w);
        }

        for (std::size_t end = input_len - 1; end > start; --end) {
            std::string follow_key = utf8_mid(input, end);
            auto follow_it = c.fzk_patterns.find(follow_key);
            if (follow_it == c.fzk_patterns.end() || follow_it->second.empty()) {
                continue;
            }
            dictionary_search(dict, search_exact, order_by_frequency, utf8_mid(input, start, end - start));
            while (auto w = dictionary_get_next(dict)) {
                for (const auto& follow : follow_it->second) {
                    if (connectible(c, w->part_of_speech.right, follow.part_of_speech.left)) {
                        word combined;
                        combined.candidate = key;
                        combined.stroke = key;
                        combined.part_of_speech = { w->part_of_speech.left, follow.part_of_speech.right };
                        fzks.push_back(combined);
                    }
                }
            }
        }

        c.fzk_patterns[key] = fzks;
    }
    return c.fzk_patterns[input];
}

static void add_auto_generated_candidates(clause_converter& c, const std::string& input, std::vector<word>& words)
{
    word generated;
    generated.candidate = input;
    generated.stroke = input;
    generated.part_of_speech = c.pos_default;
    generated.frequency = (clause_cost - 1) * static_cast<int>(utf8_codepoint_count(input));
    words.push_back(generated);
}

static std::vector<word> get_independent_words(clause_converter& c, const std::string& input, bool all)
{
    if (input.empty()) {
        return {};
    }

    auto& bag = all ? c.all_indep_word_bag : c.indep_word_bag;
    if (auto it = bag.find(input); it != bag.end()) {
        return it->second;
    }

    std::vector<word> words;
    auto& dict = *c.dictionary;
    dictionary_clear(dict);
    dictionary_clear_approx(dict);
    dictionary_set(dict, 4, 0, 10);
    dictionary_set(dict, 5, 400, 500);
    dictionary_set(dict, -1, freq_user, freq_user);
    dictionary_set(dict, -2, freq_learn, freq_learn);

    dictionary_search(dict, search_exact, order_by_frequency, input);
    if (all) {
        while (auto w = dictionary_get_next(dict)) {
            if (w->stroke == input) {
                words.push_back(*w);
            }
        }
    } else {
        while (auto w = dictionary_get_next(dict)) {
            if (w->stroke == input) {
                bool found = std::any_of(words.begin(), words.end(), [&](const word& known) {
                    return known.part_of_speech.right == w->part_of_speech.right;
                });
                if (!found) {
                    words.push_back(*w);
                }
                if (w->frequency < 400) {
                    break;
                }
            }
        }
    }

    add_auto_generated_candidates(c, input, words);
    bag[input] = words;
    return words;
}

static word default_clause_word(const clause_converter& c, const std::string& input)
{
    word w;
    w.candidate = input;
    w.stroke = input;
    w.part_of_speech = c.pos_default;
    w.frequency = (clause_cost - 1) * static_cast<int>(utf8_codepoint_count(input));
    return w;
}

static bool same_sentence_word(const sentence& lhs, const sentence& rhs)
{
    return same_clause_word(lhs.value, rhs.value);
}

static void insert_sentence(std::vector<sentence>& list, const sentence& value)
{
    auto same = std::find_if(list.begin(), list.end(), [&](const sentence& x) {
        return same_sentence_word(x, value);
    });
    if (same != list.end()) {
        if (same->value.frequency >= value.value.frequency) {
            return;
        }
        list.erase(same);
    }

    auto it = std::find_if(list.begin(), list.end(), [&](const sentence& x) {
        return x.value.frequency < value.value.frequency;
    });
    list.insert(it, value);
    if (list.size() > sentence_beam_width) {
        list.resize(sentence_beam_width);
    }
}

static std::optional<sentence> consecutive_clause_convert(clause_converter& c, const std::string& input)
{
    const std::size_t input_len = utf8_codepoint_count(input);
    if (input_len == 0) {
        return std::nullopt;
    }

    std::vector<std::vector<sentence>> sentences(input_len);
    for (std::size_t start = 0; start < input_len; start++) {
        if (start != 0 && sentences[start - 1].empty()) {
            continue;
        }

        std::size_t end = std::min(input_len, start + 20);
        for (; end > start; --end) {
            std::size_t idx = end - 1;
            if (!sentences[idx].empty()) {
                int base = (start != 0 && !sentences[start - 1].empty())
                    ? sentences[start - 1].front().value.frequency
                    : 0;
                if (sentences[idx].front().value.frequency > base + clause_cost + freq_learn) {
                    break;
                }
            }

            std::string key = utf8_mid(input, start, end - start);
            std::vector<clause> clauses;
            if (end == input_len) {
                single_clause_convert(c, clauses, key, c.pos_end_clause_1, false);
            } else {
                single_clause_convert(c, clauses, key, c.pos_end_clause_3, false);
            }
            if (clauses.empty()) {
                clauses.push_back(clause{ default_clause_word(c, key) });
            }

            for (const auto& best : clauses) {
                if (start == 0) {
                    sentence ws;
                    ws.value = best.value;
                    ws.value.stroke = key;
                    ws.elements.push_back(best);
                    ws.value.frequency += clause_cost;
                    insert_sentence(sentences[idx], ws);
                    continue;
                }

                for (const auto& prev : sentences[start - 1]) {
                    sentence ws = prev;
                    ws.value.candidate += best.value.candidate;
                    ws.value.stroke += best.value.stroke;
                    ws.value.frequency += best.value.frequency;
                    ws.value.part_of_speech.right = best.value.part_of_speech.right;
                    ws.value.attribute = 2;
                    ws.elements.push_back(best);
                    ws.value.frequency += clause_cost;
                    insert_sentence(sentences[idx], ws);
                }
            }
        }
    }

    if (sentences[input_len - 1].empty()) {
        return std::nullopt;
    }
    return sentences[input_len - 1].front();
}

static void clause_converter_init(clause_converter& c, dictionary_work& dict)
{
    c.dictionary = &dict;
    c.connect_matrix = dictionary_get_connect_matrix(dict);
    c.indep_word_bag.clear();
    c.all_indep_word_bag.clear();
    c.fzk_patterns.clear();
    c.pos_default = dictionary_get_pos(dict, pos_type_meisi);
    c.pos_end_clause_1 = dictionary_get_pos(dict, pos_type_v1);
    c.pos_end_clause_2 = dictionary_get_pos(dict, pos_type_v2);
    c.pos_end_clause_3 = dictionary_get_pos(dict, pos_type_v3);
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
        out.push_back({ w.candidate, w.stroke, w.frequency, w.part_of_speech, w.attribute });
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
        dictionary_set(dict, 2, 245, 245);
        dictionary_set(dict, 3, 100, 244);
        dictionary_set(dict, -2, freq_learn, freq_learn);
    } else {
        dictionary_set(dict, 0, 100, 400);
        if (input_len > 1) {
            dictionary_set(dict, 1, 100, 400);
        }
        dictionary_set(dict, 2, 245, 245);
        dictionary_set(dict, 3, 100, 244);
        dictionary_set(dict, -1, freq_user, freq_user);
        dictionary_set(dict, -2, freq_learn, freq_learn);
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
    return e;
}

void engine_reset(engine& e)
{
    dictionary_init(e.dictionary);
    dictionary_clear(e.dictionary);
    dictionary_clear_approx(e.dictionary);
    clause_converter_init(e.converter, e.dictionary);
}

std::vector<candidate> engine_predict(engine& e, const std::string& utf8_hiragana, std::size_t limit)
{
    auto input_len = validated_utf8_codepoint_count(utf8_hiragana);
    if (!input_len || *input_len == 0 || limit == 0) {
        return {};
    }
    set_dictionary_for_prediction(e.dictionary, *input_len);
    dictionary_search(e.dictionary, search_prefix, order_by_frequency, utf8_hiragana);

    std::vector<word> words;
    while (auto w = dictionary_get_next(e.dictionary)) {
        words.push_back(*w);
        if (words.size() >= limit) {
            break;
        }
    }
    return words_to_candidates(words, limit);
}

std::vector<candidate> engine_convert(engine& e, const std::string& utf8_hiragana, std::size_t limit)
{
    auto input_len = validated_utf8_codepoint_count(utf8_hiragana);
    if (!input_len || *input_len == 0 || limit == 0 || *input_len > max_input_length) {
        return {};
    }

    std::vector<word> words;
    if (auto s = consecutive_clause_convert(e.converter, utf8_hiragana)) {
        words.push_back(s->value);
        for (const auto& cl : s->elements) {
            words.push_back(cl.value);
        }
    }

    std::vector<clause> single_clauses;
    single_clause_convert(e.converter, single_clauses, utf8_hiragana, e.converter.pos_end_clause_2, true);
    for (const auto& cl : single_clauses) {
        words.push_back(cl.value);
    }

    word raw;
    raw.candidate = utf8_hiragana;
    raw.stroke = utf8_hiragana;
    raw.part_of_speech = e.converter.pos_default;
    words.push_back(raw);

    return words_to_candidates(words, limit);
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

} // namespace openwnn
