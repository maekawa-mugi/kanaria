#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "kanaria.h"

#include <msctf.h>
#include <oleauto.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static const CLSID CLSID_KanariaTextService = {
    0x897ad097, 0x189b, 0x470c, {0x98, 0xaa, 0x78, 0x82, 0x03, 0x90, 0x2f, 0x6d}};
static const GUID GUID_KanariaProfile = {
    0x50ecee90, 0x8a6c, 0x45c6, {0xa3, 0x36, 0xe8, 0x09, 0xa3, 0x74, 0x14, 0xec}};
static const GUID GUID_KanariaAttrInput = {
    0xb90c5ce4, 0xd9a3, 0x4c7c, {0x8c, 0xe5, 0x66, 0x72, 0x78, 0xc0, 0x6c, 0x82}};
static const GUID GUID_KanariaAttrConverted = {
    0xae29f29e, 0x0557, 0x4895, {0x94, 0xe7, 0xa7, 0xeb, 0x27, 0x91, 0xc0, 0xdb}};

static HINSTANCE g_module;
static LONG g_object_count;
static LONG g_lock_count;

static void safe_release(IUnknown* p)
{
    if (p != NULL) p->Release();
}

static std::wstring module_path()
{
    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring();
    return std::wstring(path, len);
}

static UINT ascii_from_key(WPARAM wparam, LPARAM lparam)
{
    UINT vkey = (UINT)wparam;
    BYTE state[256];
    WCHAR text[8];

    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return 0;
    if ('A' <= vkey && vkey <= 'Z') return (UINT)('a' + (vkey - 'A'));
    if ('0' <= vkey && vkey <= '9') return vkey;

    if (!GetKeyboardState(state)) return 0;
    int rc = ToUnicode(vkey, (UINT)((lparam >> 16) & 0xff), state, text, 8, 0);
    if (rc == 1 && 0x20 <= text[0] && text[0] <= 0x7e) return (UINT)text[0];
    return 0;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (max_value < min_value) return min_value;
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static HRESULT guarded_property_clear(ITfProperty* property, TfEditCookie ec, ITfRange* range)
{
    if (property == NULL || range == NULL) return E_INVALIDARG;
#ifdef _MSC_VER
    __try {
        return property->Clear(ec, range);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
#else
    return property->Clear(ec, range);
#endif
}

static HRESULT guarded_property_set_value(ITfProperty* property, TfEditCookie ec, ITfRange* range, VARIANT* value)
{
    if (property == NULL || range == NULL || value == NULL) return E_INVALIDARG;
#ifdef _MSC_VER
    __try {
        return property->SetValue(ec, range, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
#else
    return property->SetValue(ec, range, value);
#endif
}

static HRESULT guarded_get_text_ext(ITfContextView* view, TfEditCookie ec, ITfRange* range, RECT* rect, BOOL* clipped)
{
    if (view == NULL || range == NULL || rect == NULL || clipped == NULL) return E_INVALIDARG;
#ifdef _MSC_VER
    __try {
        return view->GetTextExt(ec, range, rect, clipped);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
#else
    return view->GetTextExt(ec, range, rect, clipped);
#endif
}

static std::wstring utf8_to_wide(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), NULL, 0);
    if (len <= 0) return std::wstring();
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), out.data(), len);
    return out;
}

static std::size_t utf8_codepoint_count(const std::string& s)
{
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xc0) != 0x80) ++n;
    }
    return n;
}

static void pop_utf8_char(std::string& s)
{
    if (s.empty()) return;
    std::size_t pos = s.size() - 1;
    while (pos > 0 && ((unsigned char)s[pos] & 0xc0) == 0x80) --pos;
    s.erase(pos);
}

class KanariaEngine {
public:
    KanariaEngine() : engine_(kanaria::engine_create()) {}

    bool ready() const { return engine_ != nullptr; }

    bool key_ascii(unsigned int ch)
    {
        if (!ready() || ch < 0x20 || ch > 0x7e) return false;
        if (kana_preedit_) {
            kana_utf8_.clear();
            kana_preedit_ = false;
        }
        roman_.push_back((char)std::tolower((int)ch));
        clear_conversion();
        return true;
    }

    bool backspace()
    {
        if (converting_) {
            clear_conversion();
            return true;
        }
        if (kana_preedit_ && !kana_utf8_.empty()) {
            pop_utf8_char(kana_utf8_);
            if (kana_utf8_.empty()) kana_preedit_ = false;
            return true;
        }
        if (roman_.empty()) return false;
        roman_.pop_back();
        clear_conversion();
        return true;
    }

    void cancel()
    {
        roman_.clear();
        kana_utf8_.clear();
        kana_preedit_ = false;
        clear_conversion();
    }

    bool start_conversion()
    {
        clear_conversion();
        if (!make_kana()) return false;

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

    int next_candidate()
    {
        if (candidates_.empty()) return 0;
        candidate_index_ = (candidate_index_ + 1) % (int)candidates_.size();
        converting_ = true;
        return candidate_index_;
    }

    int prev_candidate()
    {
        if (candidates_.empty()) return 0;
        candidate_index_ = (candidate_index_ + (int)candidates_.size() - 1) % (int)candidates_.size();
        converting_ = true;
        return candidate_index_;
    }

    std::wstring preedit()
    {
        if (converting_ && !candidates_.empty()) return utf8_to_wide(candidates_[(size_t)candidate_index_].candidate);
        if (!make_kana()) return std::wstring();
        return utf8_to_wide(kana_utf8_);
    }

    std::wstring commit()
    {
        std::wstring text;
        if (converting_ && !candidates_.empty()) {
            const kanaria::candidate& selected = candidates_[(size_t)candidate_index_];
            text = utf8_to_wide(selected.candidate);
            kanaria::engine_learn_candidate(*engine_, selected);
        } else if (make_kana()) {
            text = utf8_to_wide(kana_utf8_);
        }
        cancel();
        return text;
    }

    int candidate_count() const { return (int)candidates_.size(); }
    int candidate_index() const { return candidate_index_; }
    bool is_converting() const { return converting_; }
    bool has_text() const { return !roman_.empty() || !kana_utf8_.empty() || converting_; }

    std::wstring candidate(int index) const
    {
        if (index < 0 || index >= (int)candidates_.size()) return std::wstring();
        return utf8_to_wide(candidates_[(size_t)index].candidate);
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
        if (kana_preedit_ && !kana_utf8_.empty()) return true;
        if (roman_.empty()) return false;
        kana_utf8_ = kanaria::romaji_to_hiragana(roman_);
        return !kana_utf8_.empty();
    }

    kanaria::engine_ptr engine_;
    std::string roman_;
    std::string kana_utf8_;
    std::vector<kanaria::candidate> candidates_;
    int candidate_index_ = 0;
    bool converting_ = false;
    bool kana_preedit_ = false;
};

class CandidateWindow {
public:
    CandidateWindow() : hwnd_(NULL), selected_(0) { SetRect(&anchor_, 0, 0, 0, 0); }
    ~CandidateWindow()
    {
        if (hwnd_ != NULL) DestroyWindow(hwnd_);
    }

    void Hide()
    {
        if (hwnd_ != NULL) ShowWindow(hwnd_, SW_HIDE);
    }

    void Show(const std::vector<std::wstring>& candidates, int selected, const RECT& anchor)
    {
        candidates_ = candidates;
        selected_ = selected;
        anchor_ = anchor;
        if (candidates_.empty()) {
            Hide();
            return;
        }
        if (!ensure_window()) return;

        int visible = visible_count();
        int width = measure_width();
        int height = row_height() * visible + footer_height() + 2;
        bool valid = anchor_.right > anchor_.left && anchor_.bottom > anchor_.top;
        int x = valid ? anchor_.left : 0;
        int y = valid ? anchor_.bottom + 4 : 0;

        if (!valid || (x <= 0 && y <= 0)) {
            GUITHREADINFO info;
            memset(&info, 0, sizeof(info));
            info.cbSize = sizeof(info);
            if (GetGUIThreadInfo(0, &info) && info.rcCaret.right >= info.rcCaret.left) {
                anchor_ = info.rcCaret;
                x = info.rcCaret.left;
                y = info.rcCaret.bottom + 4;
                valid = info.rcCaret.bottom > info.rcCaret.top;
            }
        }
        if (x <= 0 && y <= 0) {
            POINT pt;
            GetCursorPos(&pt);
            SetRect(&anchor_, pt.x, pt.y, pt.x + 1, pt.y + 20);
            x = pt.x;
            y = pt.y + 20;
            valid = true;
        }

        RECT probe = {x, y, x + width, y + height};
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        HMONITOR monitor = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
        if (monitor != NULL && GetMonitorInfoW(monitor, &mi)) {
            RECT work = mi.rcWork;
            int below = anchor_.bottom + 4;
            int above = anchor_.top - height - 4;
            x = clamp_int(anchor_.left, work.left, work.right - width);
            if (valid && below + height <= work.bottom)
                y = below;
            else if (valid && above >= work.top)
                y = above;
            else
                y = clamp_int(y, work.top, work.bottom - height);
        }

        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd_, NULL, TRUE);
    }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        CandidateWindow* self = (CandidateWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
            self = (CandidateWindow*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (self == NULL) return DefWindowProcW(hwnd, msg, wparam, lparam);
        if (msg == WM_PAINT) {
            self->paint(hwnd);
            return 0;
        }
        if (msg == WM_ERASEBKGND) return 1;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    bool ensure_window()
    {
        static ATOM atom = 0;
        if (atom == 0) {
            WNDCLASSW wc;
            memset(&wc, 0, sizeof(wc));
            wc.lpfnWndProc = CandidateWindow::wnd_proc;
            wc.hInstance = g_module;
            wc.lpszClassName = L"KanariaTsfCandidateWindow";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            atom = RegisterClassW(&wc);
            if (atom == 0) return false;
        }
        if (hwnd_ == NULL) {
            hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                    L"KanariaTsfCandidateWindow",
                                    L"Kanaria",
                                    WS_POPUP | WS_BORDER,
                                    0,
                                    0,
                                    1,
                                    1,
                                    NULL,
                                    NULL,
                                    g_module,
                                    this);
        }
        return hwnd_ != NULL;
    }

    int page_start() const { return (selected_ / page_size()) * page_size(); }
    int visible_count() const { return std::max(1, std::min(page_size(), (int)candidates_.size() - page_start())); }
    int page_size() const { return 9; }
    int row_height() const { return 24; }
    int footer_height() const { return 20; }

    int measure_width() const
    {
        HDC hdc = GetDC(NULL);
        int width = 220;
        if (hdc != NULL) {
            int start = page_start();
            int end = std::min((int)candidates_.size(), start + page_size());
            for (int i = start; i < end; ++i) {
                SIZE sz;
                std::wstring label = std::to_wstring(i + 1) + L". " + candidates_[i];
                if (GetTextExtentPoint32W(hdc, label.c_str(), (int)label.size(), &sz)) {
                    width = std::max(width, (int)sz.cx + 28);
                }
            }
            ReleaseDC(NULL, hdc);
        }
        return std::min(std::max(width, 220), 520);
    }

    void paint(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH sel = CreateSolidBrush(RGB(0, 120, 215));
        HBRUSH line = CreateSolidBrush(RGB(230, 230, 230));
        int start = page_start();
        int end = std::min((int)candidates_.size(), start + page_size());

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, bg);
        SetBkMode(hdc, TRANSPARENT);

        for (int i = start; i < end; ++i) {
            RECT row = {2, 2 + (i - start) * row_height(), rc.right - 2, 2 + (i - start + 1) * row_height()};
            std::wstring label = std::to_wstring(i + 1) + L". " + candidates_[i];
            if (i == selected_) {
                FillRect(hdc, &row, sel);
                SetTextColor(hdc, RGB(255, 255, 255));
            } else {
                SetTextColor(hdc, RGB(20, 20, 20));
            }
            row.left += 8;
            DrawTextW(hdc, label.c_str(), (int)label.size(), &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT divider = {2, rc.bottom - footer_height(), rc.right - 2, rc.bottom - footer_height() + 1};
        RECT foot = {8, rc.bottom - footer_height() + 2, rc.right - 8, rc.bottom - 2};
        FillRect(hdc, &divider, line);
        SetTextColor(hdc, RGB(90, 90, 90));
        std::wstring status = std::to_wstring(selected_ + 1) + L"/" + std::to_wstring(candidates_.size());
        DrawTextW(hdc, status.c_str(), (int)status.size(), &foot, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(bg);
        DeleteObject(sel);
        DeleteObject(line);
        EndPaint(hwnd, &ps);
    }

    HWND hwnd_;
    std::vector<std::wstring> candidates_;
    int selected_;
    RECT anchor_;
};

class KanariaTextService;

enum EditOperation {
    EditUpdatePreedit,
    EditCommitText,
    EditClearPreedit
};

static TF_DISPLAYATTRIBUTE make_display_attr(BOOL bold, TF_DA_ATTR_INFO attr_info)
{
    TF_DISPLAYATTRIBUTE attr;
    memset(&attr, 0, sizeof(attr));
    attr.crText.type = TF_CT_NONE;
    attr.crBk.type = TF_CT_NONE;
    attr.crLine.type = TF_CT_SYSCOLOR;
    attr.crLine.nIndex = COLOR_HIGHLIGHT;
    attr.lsStyle = TF_LS_SOLID;
    attr.fBoldLine = bold;
    attr.bAttr = attr_info;
    return attr;
}

class KanariaDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    KanariaDisplayAttributeInfo(const GUID& guid, const WCHAR* description, const TF_DISPLAYATTRIBUTE& attr)
        : ref_(1), guid_(guid), description_(description), attr_(attr), default_attr_(attr) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
    {
        if (ppvObject == NULL) return E_INVALIDARG;
        *ppvObject = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
            *ppvObject = (ITfDisplayAttributeInfo*)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef(void) { return (ULONG)InterlockedIncrement(&ref_); }

    STDMETHODIMP_(ULONG) Release(void)
    {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP GetGUID(GUID* pguid)
    {
        if (pguid == NULL) return E_INVALIDARG;
        *pguid = guid_;
        return S_OK;
    }

    STDMETHODIMP GetDescription(BSTR* pbstrDesc)
    {
        if (pbstrDesc == NULL) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(description_);
        return *pbstrDesc != NULL ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda)
    {
        if (pda == NULL) return E_INVALIDARG;
        *pda = attr_;
        return S_OK;
    }

    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda)
    {
        if (pda == NULL) return E_INVALIDARG;
        attr_ = *pda;
        return S_OK;
    }

    STDMETHODIMP Reset(void)
    {
        attr_ = default_attr_;
        return S_OK;
    }

private:
    LONG ref_;
    GUID guid_;
    const WCHAR* description_;
    TF_DISPLAYATTRIBUTE attr_;
    TF_DISPLAYATTRIBUTE default_attr_;
};

class KanariaDisplayAttributeEnum : public IEnumTfDisplayAttributeInfo {
public:
    KanariaDisplayAttributeEnum() : ref_(1), index_(0) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
    {
        if (ppvObject == NULL) return E_INVALIDARG;
        *ppvObject = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
            *ppvObject = (IEnumTfDisplayAttributeInfo*)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef(void) { return (ULONG)InterlockedIncrement(&ref_); }

    STDMETHODIMP_(ULONG) Release(void)
    {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum)
    {
        if (ppEnum == NULL) return E_INVALIDARG;
        KanariaDisplayAttributeEnum* clone = new KanariaDisplayAttributeEnum();
        if (clone == NULL) return E_OUTOFMEMORY;
        clone->index_ = index_;
        *ppEnum = clone;
        return S_OK;
    }

    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched)
    {
        ULONG fetched = 0;
        if (rgInfo == NULL) return E_INVALIDARG;
        while (fetched < ulCount && index_ < 2) {
            rgInfo[fetched] = make_info(index_);
            if (rgInfo[fetched] == NULL) break;
            ++fetched;
            ++index_;
        }
        if (pcFetched != NULL) *pcFetched = fetched;
        return fetched == ulCount ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset(void)
    {
        index_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG ulCount)
    {
        index_ = std::min<ULONG>(2, index_ + ulCount);
        return index_ < 2 ? S_OK : S_FALSE;
    }

    static ITfDisplayAttributeInfo* make_info(ULONG index)
    {
        if (index == 0) {
            TF_DISPLAYATTRIBUTE attr = make_display_attr(FALSE, TF_ATTR_INPUT);
            return new KanariaDisplayAttributeInfo(GUID_KanariaAttrInput, L"Kanaria Input", attr);
        }
        if (index == 1) {
            TF_DISPLAYATTRIBUTE attr = make_display_attr(TRUE, TF_ATTR_TARGET_CONVERTED);
            return new KanariaDisplayAttributeInfo(GUID_KanariaAttrConverted, L"Kanaria Converted", attr);
        }
        return NULL;
    }

private:
    LONG ref_;
    ULONG index_;
};

class KanariaEditSession : public ITfEditSession {
public:
    KanariaEditSession(KanariaTextService* owner,
                       ITfContext* context,
                       EditOperation operation,
                       const std::wstring& text,
                       LONG committed_len,
                       LONG preedit_len,
                       TfGuidAtom attr_atom);
    virtual ~KanariaEditSession();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    LONG ref_;
    KanariaTextService* owner_;
    ITfContext* context_;
    EditOperation operation_;
    std::wstring text_;
    LONG committed_len_;
    LONG preedit_len_;
    TfGuidAtom attr_atom_;
};

class KanariaTextService : public ITfTextInputProcessor, public ITfKeyEventSink, public ITfDisplayAttributeProvider {
public:
    KanariaTextService()
        : ref_(1),
          thread_mgr_(NULL),
          client_id_(TF_CLIENTID_NULL),
          preedit_len_(0),
          attr_input_atom_(TF_INVALID_GUIDATOM),
          attr_converted_atom_(TF_INVALID_GUIDATOM)
    {
        SetRect(&caret_rect_, 0, 0, 0, 0);
        InterlockedIncrement(&g_object_count);
    }

    virtual ~KanariaTextService()
    {
        deactivate();
        InterlockedDecrement(&g_object_count);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
    {
        if (ppvObject == NULL) return E_INVALIDARG;
        *ppvObject = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
            *ppvObject = (ITfTextInputProcessor*)this;
        } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
            *ppvObject = (ITfKeyEventSink*)this;
        } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
            *ppvObject = (ITfDisplayAttributeProvider*)this;
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef(void) { return (ULONG)InterlockedIncrement(&ref_); }

    STDMETHODIMP_(ULONG) Release(void)
    {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid)
    {
        if (ptim == NULL) return E_INVALIDARG;
        thread_mgr_ = ptim;
        thread_mgr_->AddRef();
        client_id_ = tid;
        engine_ = std::make_unique<KanariaEngine>();
        if (!engine_ || !engine_->ready()) {
            deactivate();
            return E_OUTOFMEMORY;
        }
        register_display_attribute_atoms();

        ITfKeystrokeMgr* keys = NULL;
        HRESULT hr = thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keys);
        if (SUCCEEDED(hr) && keys != NULL) {
            hr = keys->AdviseKeyEventSink(client_id_, (ITfKeyEventSink*)this, TRUE);
            keys->Release();
        }
        if (FAILED(hr)) deactivate();
        return hr;
    }

    STDMETHODIMP Deactivate(void)
    {
        deactivate();
        return S_OK;
    }

    STDMETHODIMP OnSetFocus(BOOL fForeground)
    {
        if (!fForeground) window_.Hide();
        return S_OK;
    }

    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
    {
        (void)pic;
        if (pfEaten == NULL) return E_INVALIDARG;
        *pfEaten = is_handled_key(wParam, lParam) ? TRUE : FALSE;
        return S_OK;
    }

    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
    {
        if (pfEaten == NULL) return E_INVALIDARG;
        *pfEaten = FALSE;
        if (pic == NULL || !is_handled_key(wParam, lParam)) return S_OK;
        *pfEaten = TRUE;

        if (wParam == VK_BACK) {
            engine_->backspace();
            if (engine_->has_text())
                update_preedit(pic);
            else
                clear_preedit(pic);
            hide_candidates();
            return S_OK;
        }

        if (wParam == VK_ESCAPE) {
            engine_->cancel();
            clear_preedit(pic);
            hide_candidates();
            return S_OK;
        }

        if (wParam == VK_RETURN) {
            commit(pic);
            hide_candidates();
            return S_OK;
        }

        if (wParam == VK_UP || wParam == VK_DOWN) {
            if (wParam == VK_UP)
                engine_->prev_candidate();
            else
                engine_->next_candidate();
            if (SUCCEEDED(update_preedit(pic)))
                show_candidates();
            else
                hide_candidates();
            return S_OK;
        }

        if (wParam == VK_SPACE) {
            if (!engine_->is_converting())
                engine_->start_conversion();
            else if (GetKeyState(VK_SHIFT) & 0x8000)
                engine_->prev_candidate();
            else
                engine_->next_candidate();
            if (SUCCEEDED(update_preedit(pic)))
                show_candidates();
            else
                hide_candidates();
            return S_OK;
        }

        UINT ch = ascii_from_key(wParam, lParam);
        if (ch != 0) {
            engine_->key_ascii(ch);
            update_preedit(pic);
            hide_candidates();
        }
        return S_OK;
    }

    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
    {
        (void)pic;
        (void)wParam;
        (void)lParam;
        if (pfEaten == NULL) return E_INVALIDARG;
        *pfEaten = FALSE;
        return S_OK;
    }

    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
    {
        (void)pic;
        (void)wParam;
        (void)lParam;
        if (pfEaten == NULL) return E_INVALIDARG;
        *pfEaten = FALSE;
        return S_OK;
    }

    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten)
    {
        (void)pic;
        (void)rguid;
        if (pfEaten == NULL) return E_INVALIDARG;
        *pfEaten = FALSE;
        return S_OK;
    }

    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum)
    {
        if (ppEnum == NULL) return E_INVALIDARG;
        *ppEnum = new KanariaDisplayAttributeEnum();
        return *ppEnum != NULL ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo)
    {
        if (ppInfo == NULL) return E_INVALIDARG;
        *ppInfo = NULL;
        if (IsEqualGUID(guid, GUID_KanariaAttrInput)) {
            TF_DISPLAYATTRIBUTE attr = make_display_attr(FALSE, TF_ATTR_INPUT);
            *ppInfo = new KanariaDisplayAttributeInfo(GUID_KanariaAttrInput, L"Kanaria Input", attr);
        } else if (IsEqualGUID(guid, GUID_KanariaAttrConverted)) {
            TF_DISPLAYATTRIBUTE attr = make_display_attr(TRUE, TF_ATTR_TARGET_CONVERTED);
            *ppInfo = new KanariaDisplayAttributeInfo(GUID_KanariaAttrConverted, L"Kanaria Converted", attr);
        }
        return *ppInfo != NULL ? S_OK : E_INVALIDARG;
    }

    HRESULT apply_edit(ITfContext* context,
                       TfEditCookie ec,
                       EditOperation operation,
                       const std::wstring& text,
                       LONG committed_len,
                       LONG new_preedit_len,
                       TfGuidAtom attr_atom)
    {
        TF_SELECTION selection;
        ULONG fetched = 0;
        HRESULT hr;

        if (context == NULL) return E_INVALIDARG;
        memset(&selection, 0, sizeof(selection));
        hr = context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched == 0 || selection.range == NULL) return hr;

        LONG old_preedit_len = preedit_len_;
        ITfRange* range = selection.range;
        if (preedit_len_ > 0) {
            LONG shifted = 0;
            range->ShiftStart(ec, -preedit_len_, &shifted, NULL);
            clear_attribute(context, ec, range);
        }

        if (operation == EditClearPreedit) {
            hr = range->SetText(ec, 0, L"", 0);
            preedit_len_ = 0;
        } else {
            hr = range->SetText(ec, 0, text.c_str(), (LONG)text.size());
            preedit_len_ = new_preedit_len;
        }

        if (SUCCEEDED(hr) && operation != EditClearPreedit) {
            ITfRange* attr_range = NULL;
            if (SUCCEEDED(range->Clone(&attr_range)) && attr_range != NULL) {
                LONG shifted = 0;
                if (new_preedit_len > 0 && attr_atom != TF_INVALID_GUIDATOM) {
                    attr_range->Collapse(ec, TF_ANCHOR_END);
                    attr_range->ShiftStart(ec, -new_preedit_len, &shifted, NULL);
                    apply_attribute(context, ec, attr_range, attr_atom);
                    update_caret_rect(context, ec, attr_range);
                } else {
                    update_caret_rect(context, ec, range);
                }
                attr_range->Release();
            }
        } else if (SUCCEEDED(hr) && old_preedit_len > 0) {
            update_caret_rect(context, ec, range);
        }

        range->Collapse(ec, TF_ANCHOR_END);
        selection.style.ase = TF_AE_END;
        selection.style.fInterimChar = FALSE;
        context->SetSelection(ec, 1, &selection);
        range->Release();
        return hr;
    }

private:
    void register_display_attribute_atoms()
    {
        ITfCategoryMgr* categories = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,
                                       NULL,
                                       CLSCTX_INPROC_SERVER,
                                       IID_ITfCategoryMgr,
                                       (void**)&categories))
            && categories != NULL) {
            categories->RegisterGUID(GUID_KanariaAttrInput, &attr_input_atom_);
            categories->RegisterGUID(GUID_KanariaAttrConverted, &attr_converted_atom_);
            categories->Release();
        }
    }

    void clear_attribute(ITfContext* context, TfEditCookie ec, ITfRange* range)
    {
        ITfProperty* property = NULL;
        if (context == NULL || range == NULL) return;
        if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property)) && property != NULL) {
            guarded_property_clear(property, ec, range);
            property->Release();
        }
    }

    void apply_attribute(ITfContext* context, TfEditCookie ec, ITfRange* range, TfGuidAtom atom)
    {
        ITfProperty* property = NULL;
        if (context == NULL || range == NULL || atom == TF_INVALID_GUIDATOM) return;
        if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property)) && property != NULL) {
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_I4;
            value.lVal = atom;
            guarded_property_set_value(property, ec, range, &value);
            property->Release();
        }
    }

    void deactivate()
    {
        window_.Hide();
        if (thread_mgr_ != NULL) {
            ITfKeystrokeMgr* keys = NULL;
            if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keys)) && keys != NULL) {
                keys->UnadviseKeyEventSink(client_id_);
                keys->Release();
            }
            thread_mgr_->Release();
            thread_mgr_ = NULL;
        }
        engine_.reset();
        client_id_ = TF_CLIENTID_NULL;
        preedit_len_ = 0;
    }

    bool is_handled_key(WPARAM wParam, LPARAM lParam) const
    {
        if (!engine_) return false;
        if (wParam == VK_UP || wParam == VK_DOWN) {
            return engine_->is_converting();
        }
        if (wParam == VK_ESCAPE || wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_SPACE) {
            return engine_->has_text() || engine_->is_converting();
        }
        return ascii_from_key(wParam, lParam) != 0;
    }

    HRESULT request_edit(ITfContext* context,
                         EditOperation operation,
                         const std::wstring& text,
                         LONG committed_len,
                         LONG preedit_len,
                         TfGuidAtom attr_atom)
    {
        if (context == NULL) return E_INVALIDARG;
        KanariaEditSession* session = new KanariaEditSession(this, context, operation, text, committed_len, preedit_len, attr_atom);
        if (session == NULL) return E_OUTOFMEMORY;
        HRESULT session_hr = E_FAIL;
        HRESULT hr = context->RequestEditSession(client_id_, session, TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
        session->Release();
        return SUCCEEDED(hr) ? session_hr : hr;
    }

    HRESULT update_preedit(ITfContext* context)
    {
        std::wstring text = engine_->preedit();
        if (text.empty()) {
            clear_preedit(context);
            return S_FALSE;
        }
        return request_edit(context,
                            EditUpdatePreedit,
                            text,
                            0,
                            (LONG)text.size(),
                            engine_->is_converting() ? attr_converted_atom_ : attr_input_atom_);
    }

    void clear_preedit(ITfContext* context)
    {
        request_edit(context, EditClearPreedit, std::wstring(), 0, 0, TF_INVALID_GUIDATOM);
    }

    void commit(ITfContext* context)
    {
        std::wstring text = engine_->commit();
        if (text.empty()) {
            clear_preedit(context);
            return;
        }
        request_edit(context, EditCommitText, text, (LONG)text.size(), 0, TF_INVALID_GUIDATOM);
    }

    void show_candidates()
    {
        int count = engine_->candidate_count();
        if (count <= 0) {
            hide_candidates();
            return;
        }

        std::vector<std::wstring> candidates;
        candidates.reserve((size_t)count);
        for (int i = 0; i < count; ++i) {
            std::wstring candidate = engine_->candidate(i);
            if (!candidate.empty()) candidates.push_back(candidate);
        }
        window_.Show(candidates, engine_->candidate_index(), caret_rect_);
    }

    void hide_candidates()
    {
        window_.Hide();
    }

    void update_caret_rect(ITfContext* context, TfEditCookie ec, ITfRange* range)
    {
        ITfContextView* view = NULL;
        RECT rect;
        BOOL clipped = FALSE;
        SetRect(&rect, 0, 0, 0, 0);
        if (context != NULL && range != NULL && SUCCEEDED(context->GetActiveView(&view)) && view != NULL) {
            if (SUCCEEDED(guarded_get_text_ext(view, ec, range, &rect, &clipped))) {
                caret_rect_ = rect;
            }
            view->Release();
        }
        if (caret_rect_.left == 0 && caret_rect_.right == 0) {
            GUITHREADINFO info;
            memset(&info, 0, sizeof(info));
            info.cbSize = sizeof(info);
            if (GetGUIThreadInfo(0, &info)) caret_rect_ = info.rcCaret;
        }
    }

    LONG ref_;
    ITfThreadMgr* thread_mgr_;
    TfClientId client_id_;
    std::unique_ptr<KanariaEngine> engine_;
    LONG preedit_len_;
    TfGuidAtom attr_input_atom_;
    TfGuidAtom attr_converted_atom_;
    RECT caret_rect_;
    CandidateWindow window_;
};

KanariaEditSession::KanariaEditSession(KanariaTextService* owner,
                                       ITfContext* context,
                                       EditOperation operation,
                                       const std::wstring& text,
                                       LONG committed_len,
                                       LONG preedit_len,
                                       TfGuidAtom attr_atom)
    : ref_(1),
      owner_(owner),
      context_(context),
      operation_(operation),
      text_(text),
      committed_len_(committed_len),
      preedit_len_(preedit_len),
      attr_atom_(attr_atom)
{
    if (owner_ != NULL) owner_->AddRef();
    if (context_ != NULL) context_->AddRef();
}

KanariaEditSession::~KanariaEditSession()
{
    safe_release(context_);
    if (owner_ != NULL) owner_->Release();
}

STDMETHODIMP KanariaEditSession::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == NULL) return E_INVALIDARG;
    *ppvObject = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
        *ppvObject = (ITfEditSession*)this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) KanariaEditSession::AddRef(void)
{
    return (ULONG)InterlockedIncrement(&ref_);
}

STDMETHODIMP_(ULONG) KanariaEditSession::Release(void)
{
    ULONG r = (ULONG)InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP KanariaEditSession::DoEditSession(TfEditCookie ec)
{
    if (owner_ == NULL) return E_FAIL;
    return owner_->apply_edit(context_, ec, operation_, text_, committed_len_, preedit_len_, attr_atom_);
}

class KanariaClassFactory : public IClassFactory {
public:
    KanariaClassFactory() : ref_(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
    {
        if (ppvObject == NULL) return E_INVALIDARG;
        *ppvObject = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppvObject = (IClassFactory*)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef(void) { return (ULONG)InterlockedIncrement(&ref_); }

    STDMETHODIMP_(ULONG) Release(void)
    {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject)
    {
        if (ppvObject == NULL) return E_INVALIDARG;
        *ppvObject = NULL;
        if (pUnkOuter != NULL) return CLASS_E_NOAGGREGATION;
        KanariaTextService* service = new KanariaTextService();
        if (service == NULL) return E_OUTOFMEMORY;
        HRESULT hr = service->QueryInterface(riid, ppvObject);
        service->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock)
            InterlockedIncrement(&g_lock_count);
        else
            InterlockedDecrement(&g_lock_count);
        return S_OK;
    }

private:
    LONG ref_;
};

static HRESULT write_reg_string(HKEY root, const std::wstring& subkey, const WCHAR* name, const std::wstring& value)
{
    HKEY key;
    LONG rc = RegCreateKeyExW(root, subkey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &key, NULL);
    if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
    rc = RegSetValueExW(key,
                        name,
                        0,
                        REG_SZ,
                        (const BYTE*)value.c_str(),
                        (DWORD)((value.size() + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}

static HRESULT register_com_server(void)
{
    WCHAR guid[64];
    StringFromGUID2(CLSID_KanariaTextService, guid, 64);
    std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
    HRESULT hr = write_reg_string(HKEY_CURRENT_USER, base, NULL, L"Kanaria TSF Text Service");
    if (FAILED(hr)) return hr;
    hr = write_reg_string(HKEY_CURRENT_USER, base + L"\\InprocServer32", NULL, module_path());
    if (FAILED(hr)) return hr;
    return write_reg_string(HKEY_CURRENT_USER, base + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
}

static HRESULT unregister_com_server(void)
{
    WCHAR guid[64];
    StringFromGUID2(CLSID_KanariaTextService, guid, 64);
    std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
    LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(rc);
}

static HRESULT register_tsf_profile(void)
{
    ITfInputProcessorProfiles* profiles = NULL;
    ITfCategoryMgr* categories = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfiles,
                                  (void**)&profiles);
    if (FAILED(hr)) return hr;
    hr = profiles->Register(CLSID_KanariaTextService);
    if (SUCCEEDED(hr)) {
        hr = profiles->AddLanguageProfile(CLSID_KanariaTextService,
                                          MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT),
                                          GUID_KanariaProfile,
                                          L"Kanaria",
                                          7,
                                          NULL,
                                          0,
                                          0);
    }
    profiles->Release();
    if (FAILED(hr)) return hr;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_ITfCategoryMgr,
                          (void**)&categories);
    if (SUCCEEDED(hr) && categories != NULL) {
        hr = categories->RegisterCategory(CLSID_KanariaTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_KanariaTextService);
        if (SUCCEEDED(hr)) {
            hr = categories->RegisterCategory(CLSID_KanariaTextService,
                                              GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                              CLSID_KanariaTextService);
        }
        categories->Release();
    }
    return hr;
}

static HRESULT unregister_tsf_profile(void)
{
    ITfInputProcessorProfiles* profiles = NULL;
    ITfCategoryMgr* categories = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfiles,
                                  (void**)&profiles);
    if (SUCCEEDED(hr) && profiles != NULL) {
        profiles->RemoveLanguageProfile(CLSID_KanariaTextService,
                                        MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT),
                                        GUID_KanariaProfile);
        profiles->Unregister(CLSID_KanariaTextService);
        profiles->Release();
    }
    hr = CoCreateInstance(CLSID_TF_CategoryMgr,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_ITfCategoryMgr,
                          (void**)&categories);
    if (SUCCEEDED(hr) && categories != NULL) {
        categories->UnregisterCategory(CLSID_KanariaTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_KanariaTextService);
        categories->UnregisterCategory(CLSID_KanariaTextService,
                                       GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                       CLSID_KanariaTextService);
        categories->Release();
    }
    return S_OK;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return g_object_count == 0 && g_lock_count == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject)
{
    if (ppvObject == NULL) return E_INVALIDARG;
    *ppvObject = NULL;
    if (!IsEqualCLSID(rclsid, CLSID_KanariaTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    KanariaClassFactory* factory = new KanariaClassFactory();
    if (factory == NULL) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppvObject);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer(void)
{
    HRESULT hr = register_com_server();
    if (FAILED(hr)) return hr;
    HRESULT co = CoInitialize(NULL);
    hr = register_tsf_profile();
    if (SUCCEEDED(co)) CoUninitialize();
    return hr;
}

STDAPI DllUnregisterServer(void)
{
    HRESULT co = CoInitialize(NULL);
    unregister_tsf_profile();
    if (SUCCEEDED(co)) CoUninitialize();
    return unregister_com_server();
}
