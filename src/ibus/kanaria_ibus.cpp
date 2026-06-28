#include "kanaria.h"

#include <ibus.h>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr guint lookup_page_size = 9;

static std::size_t utf8_codepoint_count(const std::string& s)
{
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xc0) != 0x80) {
            ++n;
        }
    }
    return n;
}

static void pop_utf8_char(std::string& s)
{
    if (s.empty()) {
        return;
    }
    std::size_t pos = s.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80) {
        --pos;
    }
    s.erase(pos);
}

class KanariaInputState {
public:
    KanariaInputState() : engine_(kanaria::engine_create()) {}

    bool ready() const { return engine_ != nullptr; }

    bool key_ascii(unsigned int ch)
    {
        if (!ready() || ch < 0x20 || ch > 0x7e) {
            return false;
        }
        roman_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        clear_conversion();
        return true;
    }

    bool backspace()
    {
        if (converting_) {
            clear_conversion();
            return true;
        }
        if (!kana_utf8_.empty() && roman_.empty()) {
            pop_utf8_char(kana_utf8_);
            return true;
        }
        if (roman_.empty()) {
            return false;
        }
        roman_.pop_back();
        kana_utf8_.clear();
        clear_conversion();
        return true;
    }

    void cancel()
    {
        roman_.clear();
        kana_utf8_.clear();
        clear_conversion();
    }

    bool start_conversion()
    {
        clear_conversion();
        if (!make_kana()) {
            return false;
        }

        const std::size_t len = utf8_codepoint_count(kana_utf8_);
        if (len > 0) {
            candidates_ = kanaria::engine_get_clause_candidates(*engine_, kana_utf8_, 0, len, 64);
        }
        if (candidates_.empty()) {
            auto best = kanaria::engine_convert_best(*engine_, kana_utf8_);
            if (best) {
                candidates_.push_back(kanaria::candidate{best->value.candidate,
                                                         best->value.stroke,
                                                         best->value.frequency,
                                                         best->value.connection,
                                                         best->value.attribute});
            }
        }
        if (candidates_.empty()) {
            candidates_.push_back(kanaria::candidate{kana_utf8_, kana_utf8_, 0, {}, 0});
        }
        candidate_index_ = 0;
        converting_ = true;
        return true;
    }

    bool next_candidate()
    {
        if (candidates_.empty()) {
            return false;
        }
        candidate_index_ = (candidate_index_ + 1) % static_cast<int>(candidates_.size());
        converting_ = true;
        return true;
    }

    bool prev_candidate()
    {
        if (candidates_.empty()) {
            return false;
        }
        candidate_index_ = (candidate_index_ + static_cast<int>(candidates_.size()) - 1)
            % static_cast<int>(candidates_.size());
        converting_ = true;
        return true;
    }

    bool select_candidate(int index)
    {
        if (index < 0 || index >= static_cast<int>(candidates_.size())) {
            return false;
        }
        candidate_index_ = index;
        converting_ = true;
        return true;
    }

    std::string preedit()
    {
        if (converting_ && !candidates_.empty()) {
            return candidates_[static_cast<std::size_t>(candidate_index_)].candidate;
        }
        if (!make_kana()) {
            return {};
        }
        return kana_utf8_;
    }

    std::string commit()
    {
        std::string text;
        if (converting_ && !candidates_.empty()) {
            const kanaria::candidate& selected = candidates_[static_cast<std::size_t>(candidate_index_)];
            text = selected.candidate;
            kanaria::engine_learn_candidate(*engine_, selected);
        } else if (make_kana()) {
            text = kana_utf8_;
        }
        cancel();
        return text;
    }

    int candidate_count() const { return static_cast<int>(candidates_.size()); }
    int candidate_index() const { return candidate_index_; }
    bool is_converting() const { return converting_; }
    bool has_text() const { return !roman_.empty() || !kana_utf8_.empty() || converting_; }

    std::string candidate(int index) const
    {
        if (index < 0 || index >= static_cast<int>(candidates_.size())) {
            return {};
        }
        return candidates_[static_cast<std::size_t>(index)].candidate;
    }

private:
    void clear_conversion()
    {
        candidates_.clear();
        candidate_index_ = 0;
        converting_ = false;
    }

    bool make_kana()
    {
        if (!roman_.empty()) {
            kana_utf8_ = kanaria::romaji_to_hiragana(roman_);
        }
        return !kana_utf8_.empty();
    }

    kanaria::engine_ptr engine_;
    std::string roman_;
    std::string kana_utf8_;
    std::vector<kanaria::candidate> candidates_;
    int candidate_index_ = 0;
    bool converting_ = false;
};

} // namespace

typedef struct _KanariaIBusEngine KanariaIBusEngine;
typedef struct _KanariaIBusEngineClass KanariaIBusEngineClass;

struct _KanariaIBusEngine {
    IBusEngine parent;
    KanariaInputState* state;
};

struct _KanariaIBusEngineClass {
    IBusEngineClass parent;
};

#define KANARIA_TYPE_IBUS_ENGINE (kanaria_ibus_engine_get_type())
#define KANARIA_IBUS_ENGINE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), KANARIA_TYPE_IBUS_ENGINE, KanariaIBusEngine))

static void kanaria_ibus_engine_class_init(KanariaIBusEngineClass* klass);
static void kanaria_ibus_engine_init(KanariaIBusEngine* self);
GType kanaria_ibus_engine_get_type(void);

G_DEFINE_TYPE(KanariaIBusEngine, kanaria_ibus_engine, IBUS_TYPE_ENGINE)

static void update_preedit(KanariaIBusEngine* self)
{
    const std::string preedit = self->state->preedit();
    IBusText* text = ibus_text_new_from_string(preedit.c_str());
    if (!preedit.empty()) {
        ibus_text_append_attribute(text,
                                   IBUS_ATTR_TYPE_UNDERLINE,
                                   IBUS_ATTR_UNDERLINE_SINGLE,
                                   0,
                                   static_cast<guint>(utf8_codepoint_count(preedit)));
    }
    ibus_engine_update_preedit_text(IBUS_ENGINE(self),
                                    text,
                                    static_cast<guint>(utf8_codepoint_count(preedit)),
                                    !preedit.empty());
}

static void update_lookup_table(KanariaIBusEngine* self)
{
    IBusLookupTable* table = ibus_lookup_table_new(lookup_page_size, 0, TRUE, TRUE);
    ibus_lookup_table_set_cursor_pos(table, static_cast<guint>(self->state->candidate_index()));
    ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_VERTICAL);

    const int count = self->state->candidate_count();
    for (int i = 0; i < count; ++i) {
        const std::string candidate = self->state->candidate(i);
        if (!candidate.empty()) {
            ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(candidate.c_str()));
        }
    }

    ibus_engine_update_lookup_table(IBUS_ENGINE(self),
                                    table,
                                    self->state->is_converting() && count > 0);
}

static void update_user_interface(KanariaIBusEngine* self)
{
    update_preedit(self);
    update_lookup_table(self);
}

static void commit_current(KanariaIBusEngine* self)
{
    const std::string text = self->state->commit();
    if (text.empty()) {
        return;
    }
    ibus_engine_commit_text(IBUS_ENGINE(self), ibus_text_new_from_string(text.c_str()));
}

static unsigned int ascii_from_keyval(guint keyval, guint modifiers)
{
    if ((modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK)) != 0) {
        return 0;
    }
    const gunichar ch = ibus_keyval_to_unicode(keyval);
    if (ch < 0x20 || ch > 0x7e) {
        return 0;
    }
    return static_cast<unsigned int>(ch);
}

static int keyval_to_candidate_index(guint keyval)
{
    if (keyval >= IBUS_KEY_1 && keyval <= IBUS_KEY_9) {
        return static_cast<int>(keyval - IBUS_KEY_1);
    }
    if (keyval >= IBUS_KEY_KP_1 && keyval <= IBUS_KEY_KP_9) {
        return static_cast<int>(keyval - IBUS_KEY_KP_1);
    }
    return -1;
}

static gboolean kanaria_ibus_engine_process_key_event(IBusEngine* engine,
                                                      guint keyval,
                                                      guint keycode,
                                                      guint modifiers)
{
    (void)keycode;
    KanariaIBusEngine* self = KANARIA_IBUS_ENGINE(engine);
    if ((modifiers & IBUS_RELEASE_MASK) != 0 || self->state == nullptr || !self->state->ready()) {
        return FALSE;
    }

    if (self->state->is_converting()) {
        const int index = keyval_to_candidate_index(keyval);
        if (self->state->select_candidate(index)) {
            commit_current(self);
            update_user_interface(self);
            return TRUE;
        }
    }

    switch (keyval) {
    case IBUS_KEY_Escape:
        if (self->state->has_text()) {
            self->state->cancel();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_BackSpace:
        if (self->state->backspace()) {
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Return:
    case IBUS_KEY_KP_Enter:
        if (self->state->has_text()) {
            commit_current(self);
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_space:
        if (self->state->has_text()) {
            if (self->state->is_converting()) {
                self->state->next_candidate();
            } else {
                self->state->start_conversion();
            }
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Down:
    case IBUS_KEY_Right:
    case IBUS_KEY_Page_Down:
        if (self->state->is_converting()) {
            self->state->next_candidate();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Up:
    case IBUS_KEY_Left:
    case IBUS_KEY_Page_Up:
        if (self->state->is_converting()) {
            self->state->prev_candidate();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    default:
        break;
    }

    const unsigned int ch = ascii_from_keyval(keyval, modifiers);
    if (ch != 0 && self->state->key_ascii(ch)) {
        update_user_interface(self);
        return TRUE;
    }

    return FALSE;
}

static void reset_state(KanariaIBusEngine* self)
{
    if (self->state != nullptr) {
        self->state->cancel();
        update_user_interface(self);
    }
}

static void kanaria_ibus_engine_reset(IBusEngine* engine)
{
    reset_state(KANARIA_IBUS_ENGINE(engine));
    IBusEngineClass* parent = IBUS_ENGINE_CLASS(kanaria_ibus_engine_parent_class);
    if (parent->reset != nullptr) {
        parent->reset(engine);
    }
}

static void kanaria_ibus_engine_focus_out(IBusEngine* engine)
{
    reset_state(KANARIA_IBUS_ENGINE(engine));
    IBusEngineClass* parent = IBUS_ENGINE_CLASS(kanaria_ibus_engine_parent_class);
    if (parent->focus_out != nullptr) {
        parent->focus_out(engine);
    }
}

static void kanaria_ibus_engine_disable(IBusEngine* engine)
{
    reset_state(KANARIA_IBUS_ENGINE(engine));
    IBusEngineClass* parent = IBUS_ENGINE_CLASS(kanaria_ibus_engine_parent_class);
    if (parent->disable != nullptr) {
        parent->disable(engine);
    }
}

static void kanaria_ibus_engine_candidate_clicked(IBusEngine* engine,
                                                 guint index,
                                                 guint button,
                                                 guint state)
{
    (void)state;
    if (button != 1) {
        return;
    }
    KanariaIBusEngine* self = KANARIA_IBUS_ENGINE(engine);
    if (self->state->select_candidate(static_cast<int>(index))) {
        commit_current(self);
        update_user_interface(self);
    }
}

static void kanaria_ibus_engine_finalize(GObject* object)
{
    KanariaIBusEngine* self = KANARIA_IBUS_ENGINE(object);
    delete self->state;
    self->state = nullptr;

    G_OBJECT_CLASS(kanaria_ibus_engine_parent_class)->finalize(object);
}

static void kanaria_ibus_engine_class_init(KanariaIBusEngineClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = kanaria_ibus_engine_finalize;

    IBusEngineClass* engine_class = IBUS_ENGINE_CLASS(klass);
    engine_class->process_key_event = kanaria_ibus_engine_process_key_event;
    engine_class->reset = kanaria_ibus_engine_reset;
    engine_class->focus_out = kanaria_ibus_engine_focus_out;
    engine_class->disable = kanaria_ibus_engine_disable;
    engine_class->candidate_clicked = kanaria_ibus_engine_candidate_clicked;
}

static void kanaria_ibus_engine_init(KanariaIBusEngine* self)
{
    self->state = new KanariaInputState();
}

static void bus_disconnected_cb(IBusBus* bus, gpointer user_data)
{
    (void)bus;
    (void)user_data;
    ibus_quit();
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::setlocale(LC_ALL, "");
    ibus_init();

    IBusBus* bus = ibus_bus_new();
    if (bus == nullptr || !ibus_bus_is_connected(bus)) {
        g_printerr("ibus-kanaria: could not connect to ibus-daemon\n");
        if (bus != nullptr) {
            g_object_unref(bus);
        }
        return 1;
    }

    g_signal_connect(bus, "disconnected", G_CALLBACK(bus_disconnected_cb), nullptr);

    IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(bus));
    ibus_factory_add_engine(factory, "kanaria", KANARIA_TYPE_IBUS_ENGINE);
    ibus_bus_request_name(bus, "org.freedesktop.IBus.Kanaria", 0);

    ibus_main();

    g_object_unref(factory);
    g_object_unref(bus);
    return 0;
}
