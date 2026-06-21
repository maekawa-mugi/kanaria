#include "kanaria.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <class Function>
void benchmark(std::string_view name, int iterations, Function&& function)
{
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        function(i);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin);
    std::cout << name << '\t' << elapsed.count() / static_cast<double>(iterations) << " us/op\n";
}

} // namespace

int main()
{
    auto engine = kanaria::engine_create();
    constexpr int iterations = 200;
    const std::string short_input = "きょう";
    const std::string medium_input = "きょうはいいてんき";
    const std::string long_input = "わたしはがっこうにいきますきょうはいいてんき";
    const std::vector<std::string> incremental = {
        "き", "きょ", "きょう", "きょうは", "きょうはいい", "きょうはいいてんき"
    };

    benchmark("convert/cold", iterations, [&](int) {
        kanaria::engine_reset(*engine);
        (void)kanaria::engine_convert_best(*engine, medium_input);
    });
    benchmark("convert/short-warm", iterations, [&](int) {
        (void)kanaria::engine_convert_best(*engine, short_input);
    });
    benchmark("convert/medium-warm", iterations, [&](int) {
        (void)kanaria::engine_convert_best(*engine, medium_input);
    });
    benchmark("convert/long-warm", iterations, [&](int) {
        (void)kanaria::engine_convert_best(*engine, long_input);
    });
    benchmark("convert/incremental", iterations, [&](int i) {
        (void)kanaria::engine_convert_best(*engine, incremental[static_cast<std::size_t>(i) % incremental.size()]);
    });
    benchmark("prediction/warm", iterations, [&](int) {
        (void)kanaria::engine_predict(*engine, short_input, 20);
    });
    benchmark("clause-candidates/warm", iterations, [&](int) {
        (void)kanaria::engine_get_clause_candidates(*engine, medium_input, 0, 3, 100);
    });
    benchmark("convert/after-invalidation", iterations, [&](int i) {
        kanaria::word value;
        value.candidate = "計測" + std::to_string(i);
        value.stroke = "けいそく";
        value.frequency = 500;
        value.connection = { 190, 37 };
        (void)kanaria::engine_add_user_word(*engine, value);
        (void)kanaria::engine_convert_best(*engine, medium_input);
    });
}
