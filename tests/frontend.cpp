#include "kanaria_input_state.h"

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

void type_ascii(kanaria_frontend::InputState& state, const std::string& text)
{
    for (unsigned char ch : text) {
        state.key_ascii(ch);
    }
}

} // namespace

int main()
{
    kanaria_frontend::InputState state;
    bool ok = true;

    type_ascii(state, "watashinonamaehanakanodesu");
    ok &= expect(state.start_conversion(), "frontend conversion did not start");
    ok &= expect(state.candidate_count() > 0, "frontend conversion returned no candidates");
    ok &= expect(state.candidate(0) != "わたしのなまえはなかのです",
                 "frontend returned raw hiragana before sentence conversion");

    return ok ? 0 : 1;
}
