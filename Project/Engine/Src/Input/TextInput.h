#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 入力欄へ届いた 1 件（確定した文字か、編集のキー）
    struct TextInputEvent
    {
        enum class Kind : std::uint8_t
        {
            /// 確定した文字（打った文字・変換して確定した文字）
            Character,
            /// 編集のキー（矢印・Backspace・Enter・Ctrl+C など）
            Key,
        };

        Kind kind = Kind::Character;
        /// 確定した文字（`Kind::Character` のとき）
        char32_t character = 0;
        /// 仮想キーコード（`Kind::Key` のとき。`VK_LEFT` など）
        std::uint32_t key = 0;
        bool ctrl = false;
        bool shift = false;
    };

    /// @brief ゲームの入力欄へ文字を渡す口（ウィンドウのメッセージ・日本語の変換・クリップボード）
    /// @details 入力欄が打っている間（`BeginCapture` から `EndCapture` まで）だけ、ウィンドウのメッセージから
    ///          確定した文字・編集のキー・変換中の文字を受け取り、そのメッセージはエディタと OS へ渡さない。
    ///          打っている間と、打ち終えたフレームの終わりまでは、キーボードのアクションを押されていない扱いにする。
    ///          エディタを含まない構成では、打っていない間は日本語の変換を切る。
    class TextInput
    {
    public:
        static TextInput& Get();

        /// @brief ウィンドウのメッセージを見る（文字を受け取る窓のプロシージャの最初に呼ぶ）
        /// @return true なら受け取った。呼び出し側は outResult を返し、ほかへ渡さない
        bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& outResult);

        /// @brief 本体の窓を教える（クリップボードの持ち主・打っていない間に変換を切る窓）
        void SetMainWindow(HWND hwnd);

        // ===== 打つ =====

        /// @brief 打ち始める（溜まっていた文字とキーは捨てる。ほかの欄が打っていれば、その欄の分は終わる）
        /// @param allowIme 日本語の変換を使えるようにするか（数字だけの欄などは false）
        /// @return 打ち始めた欄の札（`IsCapturedBy`・`EndCapture` に渡す）
        std::uint32_t BeginCapture(bool allowIme);

        /// @brief 打ち終える（変換中の文字は取り消す）
        /// @param token `BeginCapture` が返した札。ほかの欄が打ち始めていれば何もしない
        void EndCapture(std::uint32_t token);

        /// @brief 打っている最中か
        bool IsCapturing() const { return capturing_; }

        /// @brief 札の欄が今も打っているか（ほかの欄が打ち始めた・アプリが裏へ回ったら false）
        bool IsCapturedBy(std::uint32_t token) const { return capturing_ && token != 0 && token == token_; }

        /// @brief キーボードのアクションを読まないか（打っている間と、打ち終えてから次のフレームの終わりまで）
        bool IsKeyboardBlocked() const { return capturing_ || blockedFrames_ > 0; }

        /// @brief フレームの始めに呼ぶ（打ち終えてからのフレームを数え、過ぎたらキーボードのアクションを戻す）
        void BeginFrame()
        {
            if (blockedFrames_ > 0) {
                --blockedFrames_;
            }
        }

        /// @brief 変換中の文字を確定させる（確定した文字は後で届く）
        void CompleteComposition();

        /// @brief 溜まった文字とキーを取り出す
        std::vector<TextInputEvent> TakeEvents();

        // ===== 変換中の文字 =====

        /// @brief 変換中の文字（無ければ空）
        const std::u32string& GetComposition() const { return composition_; }

        /// @brief 変換中の文字の中のカーソルの位置（文字の数）
        int GetCompositionCursor() const { return compositionCursor_; }

        /// @brief 変換の窓を寄せるカーソルの四角（画面の px）
        /// @note 変換の候補はこの四角を避けて出る。
        void SetCaretRect(const RECT& screenRect);

        // ===== クリップボード =====

        /// @brief クリップボードの文字（文字が無ければ空）
        std::u32string GetClipboardText() const;

        /// @brief クリップボードへ文字を書く
        void SetClipboardText(const std::u32string& text) const;

    private:
        TextInput() = default;
        ~TextInput() = default;
        TextInput(const TextInput&) = delete;
        TextInput& operator=(const TextInput&) = delete;

        /// @brief UTF-16 の 1 単位を文字として積む（サロゲートは 2 つ揃ってから積む）
        void PushUtf16(wchar_t unit);

        /// @brief 変換の通知から、確定した文字と変換中の文字を読む
        void ReadComposition(HWND hwnd, LPARAM flags);

        /// @brief 変換の窓と候補の窓をカーソルの四角へ寄せる
        void PlaceImeWindows(HWND hwnd) const;

        /// @brief 今打っている分を終える（変換中の文字は取り消す）
        void StopCapture();

        /// @brief 日本語の変換を窓に繋ぐか切るか
        void AssociateIme(HWND hwnd, bool enable);

        /// @brief 打っていない間の変換の状態にする（エディタは繋いだまま、ゲームは切る）
        void ApplyIdleIme();

        HWND mainHwnd_ = nullptr;
        /// 打ち始めたときにキーボードを持っていた窓（変換の繋ぎ替えをした窓）
        HWND captureHwnd_ = nullptr;
        /// 最後に変換を始めた窓
        HWND imeHwnd_ = nullptr;

        bool capturing_ = false;
        /// 今打っている欄の札（打ち始めるたびに増やす）
        std::uint32_t token_ = 0;
        /// 打ち終えた後もキーボードのアクションを読まないフレームの数
        int blockedFrames_ = 0;
        /// 打ち始めるときに変換を切ったか（打ち終えたら戻す）
        bool imeSuppressed_ = false;

        std::vector<TextInputEvent> events_;
        wchar_t pendingHighSurrogate_ = 0;

        std::u32string composition_;
        int compositionCursor_ = 0;

        RECT caretRect_{};
        bool hasCaretRect_ = false;
    };
}
