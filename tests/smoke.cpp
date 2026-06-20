#include "kanaria.h"

#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
std::string wide_to_utf8(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::vector<std::string> collect_args(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(wide_to_utf8(argv[i]));
    }
    return args;
}
#else
std::vector<std::string> collect_args(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return args;
}
#endif

int run_smoke(const std::vector<std::string>& args)
{
    std::string input;
    bool romaji = false;

    for (const auto& arg : args) {
        if (arg == "--romaji" || arg == "-r") {
            romaji = true;
            continue;
        }
        if (!input.empty()) {
            input += ' ';
        }
        input += arg;
    }

    if (input.empty()) {
        std::cout << "hiragana> ";
        std::getline(std::cin, input);
    }

    if (romaji) {
        input = kanaria::romaji_to_hiragana(input);
        std::cout << "hiragana=" << input << "\n";
    }

    auto engine = kanaria::engine_create();
    auto candidates = kanaria::engine_convert(*engine, input, 20);
    std::cout << "count=" << candidates.size() << "\n";
    for (const auto& c : candidates) {
        std::cout << c.candidate << "\t" << c.stroke << "\t" << c.frequency << "\n";
    }
    return candidates.empty() ? 1 : 0;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    return run_smoke(collect_args(argc, argv));
}
#else
int main(int argc, char** argv)
{
    return run_smoke(collect_args(argc, argv));
}
#endif
