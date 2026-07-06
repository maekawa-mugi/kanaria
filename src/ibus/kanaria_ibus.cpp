#include "kanaria_input_state.h"

#include <ibus.h>

#include <algorithm>
#include <clocale>
#include <string>

namespace {

constexpr guint lookup_page_size = 9;

} // namespace

typedef struct _KanariaIBusEngine KanariaIBusEngine;
typedef struct _KanariaIBusEngineClass KanariaIBusEngineClass;

struct _KanariaIBusEngine {
    IBusEngine parent;
    kanaria_frontend::InputState* state;
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
                                   static_cast<guint>(kanaria_frontend::utf8_codepoint_count(preedit)));
    }
    std::size_t cursor_bytes = preedit.size();
    if (self->state->is_converting()) {
        cursor_bytes = std::min(self->state->active_clause_end(), preedit.size());
    }
    ibus_engine_update_preedit_text(IBUS_ENGINE(self),
                                    text,
                                    static_cast<guint>(kanaria_frontend::utf8_codepoint_count(
                                        preedit.substr(0, cursor_bytes))),
                                    !preedit.empty());
}

static void update_lookup_table(KanariaIBusEngine* self)
{
    IBusLookupTable* table = ibus_lookup_table_new(lookup_page_size, 0, TRUE, TRUE);
    ibus_lookup_table_set_cursor_pos(table, static_cast<guint>(self->state->candidate_page_index()));
    ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_VERTICAL);

    const int count = self->state->visible_candidate_count();
    for (int i = 0; i < count; ++i) {
        const std::string candidate = self->state->visible_candidate(i);
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
        if (self->state->select_visible_candidate(index)) {
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

    case IBUS_KEY_Tab:
    case IBUS_KEY_ISO_Left_Tab:
        if (self->state->is_converting()) {
            if ((modifiers & IBUS_SHIFT_MASK) != 0 || keyval == IBUS_KEY_ISO_Left_Tab) {
                self->state->prev_candidate();
            } else {
                self->state->next_candidate();
            }
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Right:
        if (self->state->is_converting()) {
            self->state->next_clause();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Left:
        if (self->state->is_converting()) {
            self->state->prev_clause();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Down:
        if (self->state->is_converting()) {
            self->state->next_candidate();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Up:
        if (self->state->is_converting()) {
            self->state->prev_candidate();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Page_Down:
        if (self->state->is_converting()) {
            self->state->next_page();
            update_user_interface(self);
            return TRUE;
        }
        return FALSE;

    case IBUS_KEY_Page_Up:
        if (self->state->is_converting()) {
            self->state->prev_page();
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
    if (self->state->select_visible_candidate(static_cast<int>(index))) {
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
    self->state = new kanaria_frontend::InputState();
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
