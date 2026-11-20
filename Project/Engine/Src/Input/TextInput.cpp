#include "pch.h"
#include "Input/TextInput.h"

#include <imm.h>

#include <algorithm>
#include <cstring>
#include <utility>

#pragma comment(lib, "imm32.lib")

namespace CoreEngine
{
    namespace
    {
        /// @brief キーが押されているか（メッセージを受けた時点の状態）
        bool IsKeyDown(int virtualKey)
        {
            return (::GetKeyState(virtualKey) & 0x8000) != 0;
        }

        /// @brief UTF-16 の列をコードポイント列へ直す（対にならないサロゲートは捨てる）
        std::u32string Utf16ToUtf32(const wchar_t* text, std::size_t length)
        {
            std::u32string result;
            result.reserve(length);
            for (std::size_t i = 0; i < length; ++i) {
                const char32_t unit = static_cast<char16_t>(text[i]);
                if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < length) {
                    const char32_t low = static_cast<char16_t>(text[i + 1]);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        result.push_back(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                        ++i;
                        continue;
                    }
                }
                if (unit >= 0xD800 && unit <= 0xDFFF) {
                    continue;
                }
                result.push_back(unit);
            }
            return result;
        }

        /// @brief コードポイント列を UTF-16 の列へ直す
        std::wstring Utf32ToUtf16(const std::u32string& text)
        {
            std::wstring result;
            result.reserve(text.size());
            for (const char32_t codePoint : text) {
                if (codePoint >= 0x10000 && codePoint <= 0x10FFFF) {
                    const char32_t value = codePoint - 0x10000;
                    result.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
                    result.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
                } else if (codePoint < 0xD800 || (codePoint > 0xDFFF && codePoint < 0x10000)) {
                    result.push_back(static_cast<wchar_t>(codePoint));
                }
            }
            return result;
        }

        /// @brief 変換の文字を UTF-16 で読む（無ければ空）
        std::wstring ReadImeString(HIMC context, DWORD kind)
        {
            const LONG bytes = ::ImmGetCompositionStringW(context, kind, nullptr, 0);
            if (bytes <= 0) {
                return {};
            }
            std::wstring text(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
            ::ImmGetCompositionStringW(context, kind, text.data(), static_cast<DWORD>(bytes));
            return text;
        }

        /// @brief 文字を受け取らない修飾キーか（エディタの修飾の状態がずれないよう、そのまま流す）
        bool IsModifierKey(WPARAM key)
        {
            switch (key) {
            case VK_SHIFT:
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_MENU:
            case VK_LMENU:
            case VK_RMENU:
            case VK_LWIN:
            case VK_RWIN:
                return true;
            default:
                return false;
            }
        }
    }

    TextInput& TextInput::Get()
    {
        static TextInput instance;
        return instance;
    }

    void TextInput::SetMainWindow(HWND hwnd)
    {
        mainHwnd_ = hwnd;
        if (!capturing_) {
            ApplyIdleIme();
        }
    }

    bool TextInput::HandleWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& outResult)
    {
        if (!capturing_) {
            return false;
        }

        switch (message) {
        case WM_ACTIVATEAPP:
            // ほかのアプリへ移ったら打ち終える（戻ってきたときにキーを取られたままにしない）
            if (!wparam) {
                StopCapture();
            }
            return false;

        case WM_CHAR:
            PushUtf16(static_cast<wchar_t>(wparam));
            outResult = 0;
            return true;

        case WM_KEYDOWN:
            if (IsModifierKey(wparam)) {
                return false;
            }
            // 変換中のキーは IME が使う（ここでは何もしない）
            if (wparam != VK_PROCESSKEY) {
                TextInputEvent event;
                event.kind = TextInputEvent::Kind::Key;
                event.key = static_cast<std::uint32_t>(wparam);
                event.ctrl = IsKeyDown(VK_CONTROL);
                event.shift = IsKeyDown(VK_SHIFT);
                events_.push_back(event);
            }
            outResult = 0;
            return true;

        case WM_IME_STARTCOMPOSITION:
            // 変換中の文字は入力欄の中に出すので、IME の変換の窓は出させない
            imeHwnd_ = hwnd;
            PlaceImeWindows(hwnd);
            outResult = 0;
            return true;

        case WM_IME_COMPOSITION:
            imeHwnd_ = hwnd;
            ReadComposition(hwnd, lparam);
            PlaceImeWindows(hwnd);
            outResult = 0;
            return true;

        case WM_IME_ENDCOMPOSITION:
            composition_.clear();
            compositionCursor_ = 0;
            outResult = 0;
            return true;

        case WM_IME_CHAR:
            // 確定した文字は WM_IME_COMPOSITION で受け取っている
            outResult = 0;
            return true;

        default:
            return false;
        }
    }

    std::uint32_t TextInput::BeginCapture(bool allowIme)
    {
        StopCapture();

        events_.clear();
        pendingHighSurrogate_ = 0;
        composition_.clear();
        compositionCursor_ = 0;
        hasCaretRect_ = false;

        captureHwnd_ = ::GetFocus();
        if (!captureHwnd_) {
            captureHwnd_ = mainHwnd_;
        }
        capturing_ = true;
        if (++token_ == 0) {
            token_ = 1;
        }
        imeSuppressed_ = !allowIme;
        AssociateIme(captureHwnd_, allowIme);
        return token_;
    }

    void TextInput::EndCapture(std::uint32_t token)
    {
        if (IsCapturedBy(token)) {
            StopCapture();
        }
    }

    void TextInput::StopCapture()
    {
        if (!capturing_) {
            return;
        }

        // 変換中の文字を IME に捨てさせる（残すと次に打つ欄やエディタへ流れる）
        const HWND composingHwnd = imeHwnd_ ? imeHwnd_ : captureHwnd_;
        if (!composition_.empty() && composingHwnd) {
            if (const HIMC context = ::ImmGetContext(composingHwnd)) {
                ::ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ::ImmReleaseContext(composingHwnd, context);
            }
        }

        // 打ち終えた Enter・Esc が、次のフレームに決定・戻るとして読まれないようにする
        capturing_ = false;
        blockedFrames_ = 2;
        events_.clear();
        pendingHighSurrogate_ = 0;
        composition_.clear();
        compositionCursor_ = 0;
        hasCaretRect_ = false;

        if (imeSuppressed_) {
            AssociateIme(captureHwnd_, true);
            imeSuppressed_ = false;
        }
        ApplyIdleIme();
        captureHwnd_ = nullptr;
        imeHwnd_ = nullptr;
    }

    void TextInput::CompleteComposition()
    {
        const HWND composingHwnd = imeHwnd_ ? imeHwnd_ : captureHwnd_;
        if (!capturing_ || composition_.empty() || !composingHwnd) {
            return;
        }
        if (const HIMC context = ::ImmGetContext(composingHwnd)) {
            ::ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
            ::ImmReleaseContext(composingHwnd, context);
        }
    }

    std::vector<TextInputEvent> TextInput::TakeEvents()
    {
        std::vector<TextInputEvent> taken;
        taken.swap(events_);
        return taken;
    }

    void TextInput::SetCaretRect(const RECT& screenRect)
    {
        const bool changed = !hasCaretRect_ || std::memcmp(&caretRect_, &screenRect, sizeof(RECT)) != 0;
        caretRect_ = screenRect;
        hasCaretRect_ = true;
        if (changed && capturing_ && !composition_.empty() && imeHwnd_) {
            PlaceImeWindows(imeHwnd_);
        }
    }

    std::u32string TextInput::GetClipboardText() const
    {
        if (!::IsClipboardFormatAvailable(CF_UNICODETEXT) || !::OpenClipboard(mainHwnd_)) {
            return {};
        }
        std::u32string text;
        if (const HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* const chars = static_cast<const wchar_t*>(::GlobalLock(data))) {
                text = Utf16ToUtf32(chars, std::wcslen(chars));
                ::GlobalUnlock(data);
            }
        }
        ::CloseClipboard();
        return text;
    }

    void TextInput::SetClipboardText(const std::u32string& text) const
    {
        const std::wstring wide = Utf32ToUtf16(text);
        if (!::OpenClipboard(mainHwnd_)) {
            return;
        }
        ::EmptyClipboard();
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (const HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* const destination = ::GlobalLock(memory)) {
                std::memcpy(destination, wide.c_str(), bytes);
                ::GlobalUnlock(memory);
                // 渡したメモリはクリップボードが持つ。渡せなかったときだけ自分で解く
                if (!::SetClipboardData(CF_UNICODETEXT, memory)) {
                    ::GlobalFree(memory);
                }
            } else {
                ::GlobalFree(memory);
            }
        }
        ::CloseClipboard();
    }

    void TextInput::PushUtf16(wchar_t unit)
    {
        const char32_t value = static_cast<char16_t>(unit);
        if (value >= 0xD800 && value <= 0xDBFF) {
            pendingHighSurrogate_ = unit;
            return;
        }

        char32_t codePoint = value;
        if (value >= 0xDC00 && value <= 0xDFFF) {
            if (!pendingHighSurrogate_) {
                return;
            }
            const char32_t high = static_cast<char16_t>(pendingHighSurrogate_);
            codePoint = 0x10000 + ((high - 0xD800) << 10) + (value - 0xDC00);
        }
        pendingHighSurrogate_ = 0;

        // 制御文字（Backspace・Enter・Tab・Ctrl+文字）はキーとして受け取っている
        if (codePoint < 0x20 || codePoint == 0x7F) {
            return;
        }
        TextInputEvent event;
        event.kind = TextInputEvent::Kind::Character;
        event.character = codePoint;
        events_.push_back(event);
    }

    void TextInput::ReadComposition(HWND hwnd, LPARAM flags)
    {
        const HIMC context = ::ImmGetContext(hwnd);
        if (!context) {
            return;
        }

        if (flags & GCS_RESULTSTR) {
            const std::wstring result = ReadImeString(context, GCS_RESULTSTR);
            for (const char32_t codePoint : Utf16ToUtf32(result.data(), result.size())) {
                if (codePoint < 0x20 || codePoint == 0x7F) {
                    continue;
                }
                TextInputEvent event;
                event.kind = TextInputEvent::Kind::Character;
                event.character = codePoint;
                events_.push_back(event);
            }
        }

        if (flags & GCS_COMPSTR) {
            const std::wstring composing = ReadImeString(context, GCS_COMPSTR);
            composition_ = Utf16ToUtf32(composing.data(), composing.size());

            // カーソルは UTF-16 の位置で来るので、文字の数へ直す
            int cursor = static_cast<int>(composition_.size());
            if (flags & GCS_CURSORPOS) {
                const LONG units = ::ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
                const std::size_t clamped = units < 0 ? 0 : (std::min)(static_cast<std::size_t>(units), composing.size());
                cursor = static_cast<int>(Utf16ToUtf32(composing.data(), clamped).size());
            }
            compositionCursor_ = cursor;
        } else {
            // 確定した・取り消した。変換中の文字は残っていない
            composition_.clear();
            compositionCursor_ = 0;
        }

        ::ImmReleaseContext(hwnd, context);
    }

    void TextInput::PlaceImeWindows(HWND hwnd) const
    {
        if (!hasCaretRect_ || !hwnd) {
            return;
        }
        const HIMC context = ::ImmGetContext(hwnd);
        if (!context) {
            return;
        }

        POINT topLeft{ caretRect_.left, caretRect_.top };
        POINT bottomRight{ caretRect_.right, caretRect_.bottom };
        ::ScreenToClient(hwnd, &topLeft);
        ::ScreenToClient(hwnd, &bottomRight);

        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = topLeft;
        ::ImmSetCompositionWindow(context, &composition);

        // 候補はカーソルの下に出し、カーソルの四角には重ねない
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = { topLeft.x, bottomRight.y };
        candidate.rcArea = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        ::ImmSetCandidateWindow(context, &candidate);

        ::ImmReleaseContext(hwnd, context);
    }

    void TextInput::AssociateIme(HWND hwnd, bool enable)
    {
        if (hwnd) {
            ::ImmAssociateContextEx(hwnd, nullptr, enable ? IACE_DEFAULT : 0);
        }
    }

    void TextInput::ApplyIdleIme()
    {
#ifndef CORE_EDITOR
        // ゲームの操作中にかな入力にならないよう、打っていない間は変換を切る
        AssociateIme(mainHwnd_, false);
#endif
    }
}
