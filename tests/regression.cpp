#include "openwnn_std.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    auto engine = openwnn::engine_create();
    bool ok = true;

    const auto prediction2 = openwnn::engine_predict(*engine, "きょう", 2);
    ok &= expect(prediction2.size() == 2, "prediction limit 2 was not filled");

    const auto prediction5 = openwnn::engine_predict(*engine, "きょう", 5);
    ok &= expect(prediction5.size() == 5, "prediction limit 5 was not filled");

    return ok ? 0 : 1;
}
