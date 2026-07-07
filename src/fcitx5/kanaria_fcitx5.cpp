#include "kanaria_input_state.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fcitx {

namespace {

constexpr int lookup_page_size = 9;

int key_to_candidate_index(const Key& key)
{
    const KeyList digit_keys{Key{FcitxKey_1}, Key{FcitxKey_2}, Key{FcitxKey_3},
                             Key{FcitxKey_4}, Key{FcitxKey_5}, Key{FcitxKey_6},
                             Key{FcitxKey_7}, Key{FcitxKey_8}, Key{FcitxKey_9}};
    if (const int index = key.keyListIndex(digit_keys); index >= 0) {
        return index;
    }

    const KeyList keypad_keys{Key{FcitxKey_KP_1}, Key{FcitxKey_KP_2}, Key{FcitxKey_KP_3},
                              Key{FcitxKey_KP_4}, Key{FcitxKey_KP_5}, Key{FcitxKey_KP_6},
                              Key{FcitxKey_KP_7}, Key{FcitxKey_KP_8}, Key{FcitxKey_KP_9}};
    return key.keyListIndex(keypad_keys);
}

unsigned int ascii_from_key(const Key& key)
{
    if (key.states().test(KeyState::Ctrl) || key.states().test(KeyState::Alt)) {
        return 0;
    }
    const uint32_t ch = Key::keySymToUnicode(key.sym());
    if (ch < 0x20 || ch > 0x7e) {
        return 0;
    }
    return static_cast<unsigned int>(ch);
}

bool is_shifted_ascii_upper(const Key& key, unsigned int ch)
{
    return key.states().test(KeyState::Shift) && ch >= 'A' && ch <= 'Z';
}

} // namespace

class KanariaEngine;

class KanariaCandidateWord : public CandidateWord {
public:
    KanariaCandidateWord(KanariaEngine* engine, std::string text, int index)
        : CandidateWord(Text(std::move(text))), engine_(engine), index_(index)
    {
    }

    void select(InputContext* input_context) const override;

private:
    KanariaEngine* engine_;
    int index_;
};

class KanariaCandidateList : public CandidateList,
                             public PageableCandidateList,
                             public CursorMovableCandidateList,
                             public CursorModifiableCandidateList {
public:
    explicit KanariaCandidateList(KanariaEngine* engine);

    const Text& label(int idx) const override;
    const CandidateWord& candidate(int idx) const override;
    int size() const override;
    int cursorIndex() const override;
    CandidateLayoutHint layoutHint() const override;
    bool hasPrev() const override;
    bool hasNext() const override;
    void prev() override;
    void next() override;
    bool usedNextBefore() const override;
    int totalPages() const override;
    int currentPage() const override;
    void setPage(int page) override;
    void prevCandidate() override;
    void nextCandidate() override;
    void setCursorIndex(int cursor) override;

private:
    KanariaEngine* engine_;
    std::vector<std::unique_ptr<KanariaCandidateWord>> candidate_words_;
    std::vector<Text> labels_;
};

class KanariaEngine final : public InputMethodEngine {
public:
    explicit KanariaEngine(Instance* instance) { (void)instance; }

    void activate(const InputMethodEntry& entry, InputContextEvent& event) override;
    void deactivate(const InputMethodEntry& entry, InputContextEvent& event) override;
    void reset(const InputMethodEntry& entry, InputContextEvent& event) override;
    void keyEvent(const InputMethodEntry& entry, KeyEvent& key_event) override;

    void select_candidate(InputContext* input_context, int index);
    void update_ui(InputContext* input_context);
    kanaria_frontend::InputState& state() { return state_; }

private:
    void commit_current(InputContext* input_context);

    kanaria_frontend::InputState state_;
};

KanariaCandidateList::KanariaCandidateList(KanariaEngine* engine) : engine_(engine)
{
    setPageable(this);
    setCursorMovable(this);
    setCursorModifiable(this);

    const int count = engine_->state().visible_candidate_count();
    const int page_start = engine_->state().candidate_page_start();
    for (int i = 0; i < count; ++i) {
        candidate_words_.push_back(
            std::make_unique<KanariaCandidateWord>(engine_,
                                                   engine_->state().visible_candidate(i),
                                                   page_start + i));
        labels_.push_back(Text(std::to_string(i + 1) + "."));
    }
}

const Text& KanariaCandidateList::label(int idx) const
{
    return labels_.at(static_cast<std::size_t>(idx));
}

const CandidateWord& KanariaCandidateList::candidate(int idx) const
{
    return *candidate_words_.at(static_cast<std::size_t>(idx));
}

int KanariaCandidateList::size() const
{
    return static_cast<int>(candidate_words_.size());
}

int KanariaCandidateList::cursorIndex() const
{
    return engine_->state().candidate_page_index();
}

CandidateLayoutHint KanariaCandidateList::layoutHint() const
{
    return CandidateLayoutHint::Vertical;
}

bool KanariaCandidateList::hasPrev() const
{
    return engine_->state().candidate_page_start() > 0;
}

bool KanariaCandidateList::hasNext() const
{
    const int next_start = engine_->state().candidate_page_start() + lookup_page_size;
    return next_start < engine_->state().candidate_count();
}

void KanariaCandidateList::prev()
{
    engine_->state().prev_page();
}

void KanariaCandidateList::next()
{
    engine_->state().next_page();
}

bool KanariaCandidateList::usedNextBefore() const
{
    return hasPrev();
}

int KanariaCandidateList::totalPages() const
{
    return engine_->state().candidate_page_count();
}

int KanariaCandidateList::currentPage() const
{
    if (totalPages() == 0) {
        return -1;
    }
    return engine_->state().candidate_page_start() / lookup_page_size;
}

void KanariaCandidateList::setPage(int page)
{
    engine_->state().set_candidate_page(page);
}

void KanariaCandidateList::prevCandidate()
{
    if (engine_->state().prev_candidate()) {
        return;
    }
}

void KanariaCandidateList::nextCandidate()
{
    if (engine_->state().next_candidate()) {
        return;
    }
}

void KanariaCandidateList::setCursorIndex(int cursor)
{
    engine_->state().select_visible_candidate(cursor);
}

void KanariaCandidateWord::select(InputContext* input_context) const
{
    engine_->select_candidate(input_context, index_);
}

void KanariaEngine::activate(const InputMethodEntry& entry, InputContextEvent& event)
{
    (void)entry;
    update_ui(event.inputContext());
}

void KanariaEngine::deactivate(const InputMethodEntry& entry, InputContextEvent& event)
{
    reset(entry, event);
}

void KanariaEngine::reset(const InputMethodEntry& entry, InputContextEvent& event)
{
    (void)entry;
    state_.cancel();
    update_ui(event.inputContext());
}

void KanariaEngine::commit_current(InputContext* input_context)
{
    const std::string text = state_.commit();
    if (!text.empty()) {
        input_context->commitString(text);
    }
}

void KanariaEngine::select_candidate(InputContext* input_context, int index)
{
    if (state_.select_candidate(index)) {
        commit_current(input_context);
        update_ui(input_context);
    }
}

void KanariaEngine::update_ui(InputContext* input_context)
{
    input_context->inputPanel().reset();

    const std::string preedit_string = state_.preedit();
    if (!preedit_string.empty()) {
        Text preedit;
        preedit.append(preedit_string, TextFormatFlag::Underline);
        std::size_t cursor = preedit_string.size();
        if (state_.is_converting()) {
            cursor = std::min(state_.active_clause_end(), preedit_string.size());
        }
        preedit.setCursor(cursor);
        if (input_context->capabilityFlags().test(CapabilityFlag::Preedit)) {
            input_context->inputPanel().setClientPreedit(preedit);
        } else {
            input_context->inputPanel().setPreedit(preedit);
        }
    }

    if (state_.has_candidate_window() && state_.candidate_count() > 0) {
        input_context->inputPanel().setCandidateList(std::make_unique<KanariaCandidateList>(this));
    }

    input_context->updatePreedit();
    input_context->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void KanariaEngine::keyEvent(const InputMethodEntry& entry, KeyEvent& key_event)
{
    (void)entry;
    if (key_event.isRelease() || !state_.ready()) {
        return;
    }

    InputContext* input_context = key_event.inputContext();
    const Key& key = key_event.key();
    bool handled = false;

    if (state_.is_converting()) {
        const int index = key_to_candidate_index(key);
        if (state_.select_visible_candidate(index)) {
            commit_current(input_context);
            handled = true;
        }
    }

    if (!handled && key.check(FcitxKey_Escape)) {
        if (state_.has_text()) {
            state_.cancel();
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_BackSpace)) {
        handled = state_.backspace();
    } else if (!handled && (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter))) {
        if (state_.has_text()) {
            commit_current(input_context);
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_space)) {
        if (state_.has_text()) {
            if (state_.is_converting()) {
                state_.next_candidate();
            } else {
                state_.start_conversion();
            }
            handled = true;
        }
    } else if (!handled && (key.check(FcitxKey_Tab) || key.check(FcitxKey_ISO_Left_Tab))) {
        if (state_.has_candidate_window()) {
            if (key.states().test(KeyState::Shift) || key.check(FcitxKey_ISO_Left_Tab)) {
                state_.prev_candidate();
            } else {
                state_.next_candidate();
            }
            handled = true;
        } else if (state_.has_text()) {
            state_.start_conversion();
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_Right)) {
        if (state_.has_candidate_window()) {
            if (state_.is_converting()) {
                state_.next_clause();
            } else {
                state_.next_candidate();
            }
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_Left)) {
        if (state_.has_candidate_window()) {
            if (state_.is_converting()) {
                state_.prev_clause();
            } else {
                state_.prev_candidate();
            }
            handled = true;
        }
    } else if (!handled
               && key.check(FcitxKey_Down)) {
        if (state_.is_converting()) {
            state_.next_candidate();
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_Up)) {
        if (state_.is_converting()) {
            state_.prev_candidate();
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_Page_Down)) {
        if (state_.is_converting()) {
            state_.next_page();
            handled = true;
        }
    } else if (!handled && key.check(FcitxKey_Page_Up)) {
        if (state_.is_converting()) {
            state_.prev_page();
            handled = true;
        }
    }

    if (!handled) {
        const unsigned int ch = ascii_from_key(key);
        handled = ch != 0
            && (is_shifted_ascii_upper(key, ch) ? state_.key_ascii_literal(ch)
                                                : state_.key_ascii(ch));
    }

    if (handled) {
        update_ui(input_context);
        key_event.filterAndAccept();
    }
}

class KanariaEngineFactory : public AddonFactory {
public:
    AddonInstance* create(AddonManager* manager) override
    {
        return new KanariaEngine(manager->instance());
    }
};

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(kanaria, fcitx::KanariaEngineFactory);
