#include "nj_lib.h"
#include "nj_err.h"
#include "nj_ext.h"
#include "nj_dic.h"
#include "njd.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

extern "C" {
extern NJ_UINT32 dic_size[];
extern NJ_UINT8 dic_type[];
extern NJ_UINT8* dic_data[];
extern NJ_UINT8* con_data[];
}

namespace {

constexpr uint8_t node_term = 0x80;
constexpr uint8_t node_left_exist = 0x40;
constexpr uint8_t node_data_exist = 0x20;
constexpr uint8_t node_idx_exist = 0x10;
constexpr uint8_t stem_terminator = 0x80;
constexpr int comp_dic_freq_div = 63;
constexpr int yominasi_dic_freq_div = 63;
constexpr int fdic_data_size = 10;

uint16_t rd16(const NJ_UINT8* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8) | p[0]);
}

uint32_t rd32(const NJ_UINT8* p)
{
    return (static_cast<uint32_t>(p[3]) << 24)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[1]) << 8)
        | p[0];
}

uint16_t bitfield16(uint16_t data, uint16_t pos, uint16_t width)
{
    if (width == 0) {
        return 0;
    }
    return static_cast<uint16_t>((data >> (16 - pos - width)) & (0xffffU >> (16 - width)));
}

uint32_t bitfield32(uint32_t data, uint16_t pos, uint16_t width)
{
    if (width == 0) {
        return 0;
    }
    const uint32_t mask = width >= 32 ? 0xffffffffU : (0xffffffffU >> (32 - width));
    return static_cast<uint32_t>((data >> (32 - pos - width)) & mask);
}

uint16_t bits_to_bytes(uint16_t bits)
{
    return static_cast<uint16_t>((bits + 7) >> 3);
}

uint32_t read_bits32(const NJ_UINT8* base, uint16_t bit_offset, uint16_t width)
{
    const uint16_t pos = static_cast<uint16_t>(bit_offset >> 3);
    const uint16_t bit = static_cast<uint16_t>(bit_offset & 0x0007);
    return bitfield32(rd32(base + pos), bit, width);
}

uint16_t read_bits16(const NJ_UINT8* base, uint16_t bit_offset, uint16_t width)
{
    const uint16_t pos = static_cast<uint16_t>(bit_offset >> 3);
    const uint16_t bit = static_cast<uint16_t>(bit_offset & 0x0007);
    return bitfield16(rd16(base + pos), bit, width);
}

uint32_t dic_type_of(const NJ_UINT8* h) { return rd32(h + 8); }
uint32_t dic_data_size_of(const NJ_UINT8* h) { return rd32(h + NJ_DIC_POS_DATA_SIZE); }
uint32_t dic_ext_size_of(const NJ_UINT8* h) { return rd32(h + NJ_DIC_POS_EXT_SIZE); }
uint8_t dic_fmt(const NJ_UINT8* h) { return h[0x1c] & 0x03; }

uint8_t append_yomi_flag(const NJ_UINT8* h) { return h[0x1c] & 0x80; }
const NJ_UINT8* hinsi_top(const NJ_UINT8* h) { return h + rd32(h + 0x1d); }
uint16_t fhinsi_count(const NJ_UINT8* h) { return rd16(h + 0x21); }
uint8_t hinsi_bytes(const NJ_UINT8* h) { return h[0x25]; }
const NJ_UINT8* hindo_top(const NJ_UINT8* h) { return h + rd32(h + 0x26); }
const NJ_UINT8* stem_area_top(const NJ_UINT8* h) { return h + rd32(h + 0x2b); }
uint8_t bit_candidate_len(const NJ_UINT8* h) { return h[0x2f]; }
uint8_t bit_fhinsi(const NJ_UINT8* h) { return h[0x30]; }
uint8_t bit_bhinsi(const NJ_UINT8* h) { return h[0x31]; }
uint8_t bit_hindo_len(const NJ_UINT8* h) { return h[0x32]; }
uint8_t bit_muhenkan_len(const NJ_UINT8* h) { return h[0x33]; }
uint8_t bit_yomi_len(const NJ_UINT8* h) { return h[0x35]; }
const NJ_UINT8* yomi_index_top(const NJ_UINT8* h) { return h + rd32(h + 0x42); }
uint16_t yomi_index_count(const NJ_UINT8* h) { return h[0x46]; }
uint8_t yomi_index_size(const NJ_UINT8* h) { return h[0x47]; }
const NJ_UINT8* node_area_top(const NJ_UINT8* h) { return h + rd32(h + 0x48); }
uint8_t bit_node_data_len(const NJ_UINT8* h) { return h[0x4c]; }
uint8_t bit_node_left_len(const NJ_UINT8* h) { return h[0x4d]; }
const NJ_UINT8* node_area_mid(const NJ_UINT8* h) { return node_area_top(h) + rd32(h + 0x4e); }
const NJ_UINT8* cand_index_top(const NJ_UINT8* h) { return h + rd32(h + 0x52); }

const NJ_UINT8* fdic_stem_top(const NJ_UINT8* h) { return h + rd32(h + 0x24); }
const NJ_UINT8* fdic_strs_top(const NJ_UINT8* h) { return h + rd32(h + 0x28); }

const char* dic_type_name(uint32_t type)
{
    switch (type) {
    case NJ_DIC_TYPE_JIRITSU: return "自立語辞書";
    case NJ_DIC_TYPE_FZK: return "付属語辞書";
    case NJ_DIC_TYPE_TANKANJI: return "単漢字辞書";
    case NJ_DIC_TYPE_CUSTOM_COMPRESS: return "圧縮カスタム辞書";
    case NJ_DIC_TYPE_STDFORE: return "予測辞書";
    case NJ_DIC_TYPE_FORECONV: return "予測変換辞書";
    case NJ_DIC_TYPE_YOMINASHI: return "接尾語辞書";
    case NJ_DIC_TYPE_CUSTOM_INCOMPRESS: return "未圧縮辞書";
    case NJ_DIC_TYPE_USER: return "ユーザー辞書";
    default: return "不明な辞書";
    }
}

const char* dic_type_file_name(uint32_t type)
{
    switch (type) {
    case NJ_DIC_TYPE_JIRITSU: return "independent_words";
    case NJ_DIC_TYPE_FZK: return "ancillary_words";
    case NJ_DIC_TYPE_TANKANJI: return "single_kanji";
    case NJ_DIC_TYPE_CUSTOM_COMPRESS: return "compressed_custom";
    case NJ_DIC_TYPE_STDFORE: return "prediction";
    case NJ_DIC_TYPE_FORECONV: return "predictive_conversion";
    case NJ_DIC_TYPE_YOMINASHI: return "suffix_words";
    case NJ_DIC_TYPE_CUSTOM_INCOMPRESS: return "uncompressed";
    case NJ_DIC_TYPE_USER: return "user";
    default: return "unknown";
    }
}

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

void append_codepoint_as_utf8(std::string& dst, char32_t codepoint)
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

std::string nj_to_utf8(const NJ_CHAR* src, int max_chars)
{
    std::string dst;
    dst.reserve((NJ_MAX_LEN + NJ_MAX_RESULT_LEN + NJ_TERM_LEN) * 4 + 1);

    for (int i = 0; src[i] != NJ_CHAR_NUL && i < max_chars;) {
        char32_t codepoint = src[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (!(i < max_chars - 1) || src[i + 1] < 0xDC00 || src[i + 1] > 0xDFFF) {
                break;
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            append_codepoint_as_utf8(dst, codepoint);
            i += 2;
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            break;
        } else {
            append_codepoint_as_utf8(dst, codepoint);
            i++;
        }
    }
    return dst;
}

struct node_record {
    const NJ_UINT8* ptr = nullptr;
    std::size_t size = 0;
    bool term = false;
    bool has_left = false;
    bool has_data = false;
    uint32_t left_offset = 0;
    uint32_t data_offset = 0;
    std::vector<uint8_t> indexes;
};

struct stem_record {
    uint16_t candidate_size = 0;
    uint16_t size = 0;
};

struct row_key {
    std::string yomi;
    std::string candidate;
    int freq = 0;
    int dic_index = 0;
    int f_hinsi = 0;
    int b_hinsi = 0;

    bool operator<(const row_key& other) const
    {
        return std::tie(yomi, candidate, freq, dic_index, f_hinsi, b_hinsi)
            < std::tie(other.yomi, other.candidate, other.freq, other.dic_index, other.f_hinsi, other.b_hinsi);
    }
};

class dictionary_dumper {
public:
    dictionary_dumper(bool dedupe, std::filesystem::path split_dir)
        : dedupe_(dedupe)
        , split_dir_(std::move(split_dir))
    {
    }

    void write_header()
    {
        if (!split_dir_.empty()) {
            return;
        }
        std::cout
            << "dic_type\tstem_offset\tyomi\tcandidate\tfreq\t"
            << "f_hinsi\tb_hinsi\tyomi_len\tcandidate_len\n";
    }

    void dump_all()
    {
        for (int i = 0; i < NJ_MAX_DIC; ++i) {
            if (dic_data[i] == nullptr || dic_size[i] == 0) {
                continue;
            }
            dump_dictionary(i);
        }
    }

    void dump_dictionary(int dic_index)
    {
        const NJ_UINT8* handle = dic_data[dic_index];
        const uint32_t type = dic_type_of(handle);
        switch (type) {
        case NJ_DIC_TYPE_JIRITSU:
        case NJ_DIC_TYPE_FZK:
        case NJ_DIC_TYPE_TANKANJI:
        case NJ_DIC_TYPE_STDFORE:
        case NJ_DIC_TYPE_CUSTOM_COMPRESS:
        case NJ_DIC_TYPE_FORECONV:
            dump_compressed_trie(dic_index);
            break;
        case NJ_DIC_TYPE_YOMINASHI:
            dump_yominashi(dic_index);
            break;
        case NJ_DIC_TYPE_USER:
        case NJ_DIC_TYPE_CUSTOM_INCOMPRESS:
            dump_by_search_api(dic_index);
            break;
        default:
            std::cerr << "skip dic index " << dic_index << ": unsupported type 0x"
                      << std::hex << type << std::dec << "\n";
            break;
        }
    }

private:
    node_record parse_node(const NJ_UINT8* handle, const NJ_UINT8* node) const
    {
        node_record rec;
        rec.ptr = node;
        rec.term = (node[0] & node_term) != 0;
        rec.has_left = (node[0] & node_left_exist) != 0;
        rec.has_data = (node[0] & node_data_exist) != 0;

        const uint16_t bit_idx = (node[0] & node_idx_exist) ? 8 : 4;
        const uint16_t idx_count = (node[0] & node_idx_exist)
            ? static_cast<uint16_t>((node[0] & 0x0f) + 2)
            : 1;

        uint16_t bit_all = bit_idx;
        if (rec.has_left) {
            rec.left_offset = read_bits32(node, bit_all, bit_node_left_len(handle));
            bit_all = static_cast<uint16_t>(bit_all + bit_node_left_len(handle));
        }
        if (rec.has_data) {
            rec.data_offset = read_bits32(node, bit_all, bit_node_data_len(handle));
            bit_all = static_cast<uint16_t>(bit_all + bit_node_data_len(handle));
        }

        rec.indexes.reserve(idx_count);
        for (uint16_t i = 0; i < idx_count; ++i) {
            rec.indexes.push_back(static_cast<uint8_t>(read_bits16(node, static_cast<uint16_t>(bit_all + i * 8), 8)));
        }
        rec.size = bits_to_bytes(static_cast<uint16_t>(bit_all + idx_count * 8));
        return rec;
    }

    void append_index_as_yomi_bytes(const NJ_UINT8* handle, uint8_t index, std::vector<NJ_UINT8>& out) const
    {
        if (yomi_index_count(handle) != 0) {
            const uint8_t size = yomi_index_size(handle);
            const NJ_UINT8* table = yomi_index_top(handle) + (static_cast<uint16_t>(index) - 1) * size;
            if (size == 2) {
                out.push_back(table[0]);
                out.push_back(table[1]);
            } else {
                out.push_back(table[0]);
            }
        } else {
            out.push_back(index);
        }
    }

    stem_record parse_stem(const NJ_UINT8* handle, const NJ_UINT8* stem) const
    {
        uint16_t flag_bits = bit_muhenkan_len(handle);
        if (dic_fmt(handle) != NJ_DIC_FMT_KANAKAN) {
            flag_bits++;
        }

        uint16_t bit_all = static_cast<uint16_t>(
            1 + flag_bits + bit_hindo_len(handle) + bit_fhinsi(handle) + bit_bhinsi(handle));
        const uint16_t candidate_size = read_bits16(stem, bit_all, bit_candidate_len(handle));
        bit_all = static_cast<uint16_t>(bit_all + bit_candidate_len(handle));

        uint16_t yomi_size = 0;
        if (append_yomi_flag(handle) && (stem[0] & stem_terminator)) {
            yomi_size = read_bits16(stem, bit_all, bit_yomi_len(handle));
            bit_all = static_cast<uint16_t>(bit_all + bit_yomi_len(handle));
        }

        stem_record rec;
        rec.candidate_size = candidate_size;
        rec.size = static_cast<uint16_t>(bits_to_bytes(bit_all) + candidate_size + yomi_size);
        return rec;
    }

    const NJ_UINT8* compressed_stem_end(const NJ_UINT8* handle) const
    {
        if (dic_fmt(handle) == NJ_DIC_FMT_KANAKAN) {
            return handle + NJ_DIC_COMMON_HEADER_SIZE + dic_data_size_of(handle) + dic_ext_size_of(handle) - NJ_DIC_ID_LEN;
        }
        return cand_index_top(handle);
    }

    void dump_compressed_trie(int dic_index)
    {
        const NJ_UINT8* handle = dic_data[dic_index];
        const NJ_UINT8* root = node_area_top(handle);
        const NJ_UINT8* mid = node_area_mid(handle);
        const NJ_UINT8* node_end = stem_area_top(handle);
        std::unordered_set<std::uintptr_t> visited;
        std::unordered_set<uint32_t> dumped_groups;
        std::vector<NJ_UINT8> prefix;

        walk_node_list(dic_index, root, node_end, prefix, visited, dumped_groups);
        if (mid != root) {
            walk_node_list(dic_index, mid, node_end, prefix, visited, dumped_groups);
        }
    }

    void walk_node_list(int dic_index,
                        const NJ_UINT8* start,
                        const NJ_UINT8* node_end,
                        const std::vector<NJ_UINT8>& prefix,
                        std::unordered_set<std::uintptr_t>& visited,
                        std::unordered_set<uint32_t>& dumped_groups)
    {
        const NJ_UINT8* handle = dic_data[dic_index];
        const NJ_UINT8* node = start;

        while (node >= node_area_top(handle) && node < node_end) {
            node_record rec = parse_node(handle, node);
            if (rec.size == 0 || node + rec.size > node_end) {
                return;
            }

            const std::uintptr_t node_offset = static_cast<std::uintptr_t>(node - node_area_top(handle));
            if (!visited.insert(node_offset).second) {
                if (rec.term) {
                    return;
                }
                node += rec.size;
                continue;
            }

            std::vector<NJ_UINT8> node_prefix = prefix;
            for (uint8_t index : rec.indexes) {
                append_index_as_yomi_bytes(handle, index, node_prefix);
            }

            if (rec.has_data && dumped_groups.insert(rec.data_offset).second) {
                dump_compressed_stem_group(dic_index, rec.data_offset, node_prefix);
            }

            if (rec.has_left && rec.left_offset != 0) {
                walk_node_list(dic_index, node + rec.left_offset, node_end, node_prefix, visited, dumped_groups);
            }

            if (rec.term) {
                return;
            }
            node += rec.size;
        }
    }

    void dump_compressed_stem_group(int dic_index, uint32_t data_offset, const std::vector<NJ_UINT8>& yomi)
    {
        const NJ_UINT8* handle = dic_data[dic_index];
        const NJ_UINT8* top = stem_area_top(handle);
        const NJ_UINT8* end = compressed_stem_end(handle);
        if (top + data_offset >= end) {
            return;
        }

        uint32_t current = data_offset;
        while (top + current < end) {
            const NJ_UINT8* stem = top + current;
            stem_record rec = parse_stem(handle, stem);
            if (rec.size == 0 || stem + rec.size > end) {
                return;
            }

            dump_compressed_stem(dic_index, current, yomi);
            if (stem[0] & stem_terminator) {
                return;
            }
            current += rec.size;
        }
    }

    std::size_t fill_yomi_buffer(const std::vector<NJ_UINT8>& yomi,
                                 std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN>& yomi_buf) const
    {
        const std::size_t copy_len = std::min<std::size_t>(yomi.size() / sizeof(NJ_CHAR), NJ_MAX_LEN);
        for (std::size_t i = 0; i < copy_len; ++i) {
            yomi_buf[i] = static_cast<NJ_CHAR>(
                (static_cast<NJ_UINT16>(yomi[i * 2 + 1]) << 8) | yomi[i * 2]);
        }
        yomi_buf[copy_len] = NJ_CHAR_NUL;
        return copy_len;
    }

    bool get_compressed_word(int dic_index,
                             uint32_t stem_offset,
                             uint8_t operation,
                             NJ_CHAR* yomi_data,
                             std::size_t yomi_len,
                             NJ_WORD& word) const
    {
        NJ_SEARCH_LOCATION_SET loct {};
        loct.dic_freq.base = 0;
        loct.dic_freq.high = 1000;
        loct.loct.handle = dic_data[dic_index];
        loct.loct.current = stem_offset;
        loct.loct.top = 0;
        loct.loct.bottom = stem_offset;
        loct.loct.status = static_cast<NJ_UINT8>((operation << 4) | NJ_ST_SEARCH_READY);
        loct.loct.type = dic_type[dic_index];

        word = {};
        word.yomi = yomi_data;
        word.stem.info1 = static_cast<NJ_UINT16>(yomi_len);
        return njd_b_get_word(&loct, &word) > 0;
    }

    std::string compressed_candidate_with_operation(int dic_index,
                                                    uint32_t stem_offset,
                                                    uint8_t operation,
                                                    const std::vector<NJ_UINT8>& yomi) const
    {
        std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> yomi_buf {};
        const std::size_t yomi_len = fill_yomi_buffer(yomi, yomi_buf);
        NJ_WORD word {};
        if (!get_compressed_word(dic_index, stem_offset, operation, yomi_buf.data(), yomi_len, word)) {
            return {};
        }
        std::array<NJ_CHAR, NJ_MAX_RESULT_LEN + NJ_TERM_LEN> candidate {};
        if (njd_b_get_candidate(&word, candidate.data(), sizeof(candidate)) <= 0) {
            return {};
        }
        return nj_to_utf8(candidate.data(), NJ_MAX_RESULT_LEN);
    }

    std::string compressed_stroke_with_fore(int dic_index, uint32_t stem_offset, const std::vector<NJ_UINT8>& yomi) const
    {
        std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> yomi_buf {};
        const std::size_t yomi_len = fill_yomi_buffer(yomi, yomi_buf);
        NJ_WORD word {};
        if (!get_compressed_word(dic_index, stem_offset, NJ_CUR_OP_FORE, yomi_buf.data(), yomi_len, word)) {
            return {};
        }
        std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> stroke {};
        if (njd_b_get_stroke(&word, stroke.data(), sizeof(stroke)) <= 0) {
            return {};
        }
        return nj_to_utf8(stroke.data(), NJ_MAX_LEN);
    }

    void dump_compressed_stem(int dic_index, uint32_t stem_offset, const std::vector<NJ_UINT8>& yomi)
    {
        std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> yomi_buf {};
        const std::size_t yomi_len = fill_yomi_buffer(yomi, yomi_buf);
        NJ_WORD word {};
        if (!get_compressed_word(dic_index, stem_offset, NJ_CUR_OP_COMP, yomi_buf.data(), yomi_len, word)) {
            return;
        }

        std::string stroke = compressed_stroke_with_fore(dic_index, stem_offset, yomi);
        if (stroke.empty()) {
            stroke = nj_to_utf8(yomi_buf.data(), NJ_MAX_LEN);
        }

        std::string candidate = compressed_candidate_with_operation(dic_index, stem_offset, NJ_CUR_OP_FORE, yomi);
        if (candidate.empty()) {
            candidate = compressed_candidate_with_operation(dic_index, stem_offset, NJ_CUR_OP_COMP, yomi);
        }
        if (candidate.empty()) {
            candidate = stroke;
        }

        write_row(dic_index, dic_type_of(dic_data[dic_index]), stem_offset, stroke, candidate, word);
    }

    void dump_yominashi(int dic_index)
    {
        const NJ_UINT8* handle = dic_data[dic_index];
        const NJ_UINT8* top = fdic_stem_top(handle);
        const NJ_UINT8* end = fdic_strs_top(handle);
        for (uint32_t current = 0; top + current + fdic_data_size <= end; current += fdic_data_size) {
            NJ_SEARCH_LOCATION_SET loct {};
            loct.dic_freq.base = 0;
            loct.dic_freq.high = 1000;
            loct.loct.handle = dic_data[dic_index];
            loct.loct.current = current;
            loct.loct.status = NJ_ST_SEARCH_READY;
            loct.loct.type = dic_type[dic_index];

            NJ_WORD word {};
            if (njd_f_get_word(&loct, &word) <= 0) {
                continue;
            }

            std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> stroke_buf {};
            std::array<NJ_CHAR, NJ_MAX_RESULT_LEN + NJ_TERM_LEN> candidate_buf {};
            if (njd_f_get_stroke(&word, stroke_buf.data(), sizeof(stroke_buf)) <= 0) {
                continue;
            }
            if (njd_f_get_candidate(&word, candidate_buf.data(), sizeof(candidate_buf)) <= 0) {
                continue;
            }
            write_row(dic_index,
                      dic_type_of(handle),
                      current,
                      nj_to_utf8(stroke_buf.data(), NJ_MAX_LEN),
                      nj_to_utf8(candidate_buf.data(), NJ_MAX_RESULT_LEN),
                      word);
        }
    }

    void dump_by_search_api(int dic_index)
    {
        NJ_CLASS env {};
        NJ_CURSOR cursor {};
        NJ_DIC_SET dic_set {};
        NJ_SEARCH_CACHE cache {};
        NJ_CHAR empty_yomi[1] = { NJ_CHAR_NUL };
        NJ_CHARSET charset {};
        njx_init(&env);

        dic_set.dic[dic_index].type = dic_type[dic_index];
        dic_set.dic[dic_index].handle = dic_data[dic_index];
        dic_set.dic[dic_index].srhCache = &cache;
        dic_set.dic[dic_index].dic_freq[NJ_MODE_TYPE_HENKAN].base = 0;
        dic_set.dic[dic_index].dic_freq[NJ_MODE_TYPE_HENKAN].high = 1000;
        dic_set.rHandle[NJ_MODE_TYPE_HENKAN] = con_data[0];
        dic_set.mode = NJ_CACHE_MODE_VALID;

        cursor.cond.operation = NJ_CUR_OP_FORE;
        cursor.cond.mode = NJ_CUR_MODE_YOMI;
        cursor.cond.ds = &dic_set;
        cursor.cond.yomi = empty_yomi;
        cursor.cond.charset = &charset;

        std::memcpy(&env.dic_set, &dic_set, sizeof(dic_set));
        if (njx_search_word(&env, &cursor) <= 0) {
            return;
        }

        NJ_RESULT result {};
        while (njx_get_word(&env, &cursor, &result) > 0) {
            std::array<NJ_CHAR, NJ_MAX_LEN + NJ_TERM_LEN> stroke {};
            std::array<NJ_CHAR, NJ_MAX_RESULT_LEN + NJ_TERM_LEN> candidate {};
            if (njx_get_stroke(&env, &result, stroke.data(), sizeof(stroke)) <= 0) {
                continue;
            }
            if (njx_get_candidate(&env, &result, candidate.data(), sizeof(candidate)) <= 0) {
                continue;
            }
            const uint32_t stem_offset = result.word.stem.loc.top + result.word.stem.loc.current;
            write_row(dic_index,
                      dic_type_of(dic_data[dic_index]),
                      stem_offset,
                      nj_to_utf8(stroke.data(), NJ_MAX_LEN),
                      nj_to_utf8(candidate.data(), NJ_MAX_RESULT_LEN),
                      result.word);
        }
    }

    void write_row(int dic_index,
                   uint32_t type,
                   uint32_t stem_offset,
                   const std::string& yomi,
                   const std::string& candidate,
                   const NJ_WORD& word)
    {
        const int freq = word.stem.hindo;
        const int f_hinsi = NJ_GET_FPOS_FROM_STEM(&word);
        const int b_hinsi = NJ_GET_BPOS_FROM_STEM(&word);
        const row_key key { yomi, candidate, freq, dic_index, f_hinsi, b_hinsi };
        if (dedupe_ && !seen_.insert(key).second) {
            return;
        }

        std::ostream* out = &std::cout;
        if (!split_dir_.empty()) {
            out = &split_stream(type);
        } else {
            *out << dic_type_name(type) << "\t";
        }

        *out
            << stem_offset << "\t"
            << yomi << "\t"
            << candidate << "\t"
            << freq << "\t"
            << f_hinsi << "\t"
            << b_hinsi << "\t"
            << utf8_codepoint_count(yomi) << "\t"
            << utf8_codepoint_count(candidate) << "\n";
    }

    std::ofstream& split_stream(uint32_t type)
    {
        auto it = split_streams_.find(type);
        if (it != split_streams_.end()) {
            return it->second;
        }

        std::filesystem::create_directories(split_dir_);
        auto path = split_dir_ / (std::string(dic_type_file_name(type)) + ".tsv");
        auto [inserted, _] = split_streams_.try_emplace(type, path, std::ios::binary);
        inserted->second << "\xef\xbb\xbf";
        inserted->second
            << "stem_offset\tyomi\tcandidate\tfreq\t"
            << "f_hinsi\tb_hinsi\tyomi_len\tcandidate_len\n";
        return inserted->second;
    }

    bool dedupe_ = false;
    std::filesystem::path split_dir_;
    std::set<row_key> seen_;
    std::map<uint32_t, std::ofstream> split_streams_;
};

} // namespace

int main(int argc, char** argv)
{
    bool dedupe = false;
    int only_dic = -1;
    std::filesystem::path split_dir;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dedupe") {
            dedupe = true;
        } else if (arg == "--split-dir") {
            if (i + 1 >= argc) {
                std::cerr << "--split-dir requires a directory\n";
                return 2;
            }
            split_dir = argv[++i];
        } else {
            only_dic = std::stoi(arg);
        }
    }

    dictionary_dumper dumper(dedupe, split_dir);
    dumper.write_header();
    if (only_dic >= 0) {
        dumper.dump_dictionary(only_dic);
    } else {
        dumper.dump_all();
    }
    return 0;
}
