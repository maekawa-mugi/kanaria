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
#include <vector>

extern "C" {
extern const uint32_t dic_size[];
extern const uint8_t dic_type[];
extern const uint8_t* const dic_data[];
extern const uint8_t* const con_data[];
}

namespace {

uint32_t rd32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[3]) << 24)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[1]) << 8)
        | p[0];
}

uint32_t dic_type_of(const uint8_t* h) { return rd32(h + 8); }
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

const char* dictionary_file_name(int dic_index, uint32_t type)
{
    switch (dic_index) {
    case 0:
    case 1: return "prediction";
    case 2: return "uncompressed";
    case 3: return "suffix_words";
    case 4: return "single_kanji";
    case 5: return "independent_words";
    case 6: return "ancillary_words";
    default: return type == NJ_DIC_TYPE_USER ? "user" : "unknown";
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

    for (int i = 0; src[i] != NJ_CHAR_NUL && i < max_chars; i++) {
        char32_t codepoint = src[i];
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            break;
        }
        append_codepoint_as_utf8(dst, codepoint);
    }
    return dst;
}

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
        const uint8_t* handle = dic_data[dic_index];
        const uint32_t type = dic_type_of(handle);
        switch (type) {
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
            out = &split_stream(dic_index, type);
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

    std::ofstream& split_stream(int dic_index, uint32_t type)
    {
        const std::string name = dictionary_file_name(dic_index, type);
        auto it = split_streams_.find(name);
        if (it != split_streams_.end()) {
            return it->second;
        }

        std::filesystem::create_directories(split_dir_);
        auto path = split_dir_ / (name + ".tsv");
        auto [inserted, _] = split_streams_.try_emplace(name, path, std::ios::binary);
        inserted->second << "\xef\xbb\xbf";
        inserted->second
            << "stem_offset\tyomi\tcandidate\tfreq\t"
            << "f_hinsi\tb_hinsi\tyomi_len\tcandidate_len\n";
        return inserted->second;
    }

    bool dedupe_ = false;
    std::filesystem::path split_dir_;
    std::set<row_key> seen_;
    std::map<std::string, std::ofstream> split_streams_;
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
