#pragma once

#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/ObjectId.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UIElement.h"
#include "UI/UISelectableComponent.h"
#include "UI/UITextComponent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class RectTransformComponent;
    class UIImageComponent;
    struct TextInputEvent;

    /// @brief 入力欄が受け付ける文字の種類
    enum class UIInputContentType : std::uint8_t
    {
        /// どの文字も（日本語の変換も使える）
        Standard,
        /// 整数（数字と、先頭の -）
        Integer,
        /// 小数（数字と、先頭の - と、. を 1 つ）
        Decimal,
        /// 半角の英字と数字
        Alphanumeric,
        /// どの文字も受け付けるが * で伏せて出す（日本語の変換は使わない）
        Password,
    };

    /// @brief 文字を打ち込む欄
    /// @details 枠（このコンポーネントと同じオブジェクトの UI 画像）・打った文字を出す UI テキスト・
    ///          空のときに出す置き文字で組む。押す・決定キーで打ち始め、1 行なら Enter で確定、
    ///          Esc（パッドの B）で打ち始める前の文字へ戻す。欄の外を押すと打った文字のまま終える。
    ///          打っている間は文字とキーをこの欄だけが受け取り、キーボードのアクションは押されていない扱いになる。
    ///          カーソル・選んだ範囲・変換中の文字の下線は、打っている間だけ文字の前後に重ねて描く。
    /// @note 1 行の欄は文字を横へ、複数行の欄は縦へずらして、カーソルを欄の中に入れる
    ///       （はみ出した所は、文字の親に付けた UI マスクで切る）。
    ///       打った結果は `WasChanged()`・`WasSubmitted()` で問い合わせる（スクリプトは `Update()` で見る）。
    class UIInputFieldComponent final : public UISelectableComponent
    {
    public:
        /// @brief 中身の種類の名前（`UIInputContentType` の並び）
        static constexpr const char* kContentTypeNames[] = { "なんでも", "整数", "小数", "英数字", "伏せ字" };

        UIInputFieldComponent();

        const char* GetTypeName() const override { return "UIInputField"; }

        REFLECT_DECLARE(UIInputFieldComponent)

        /// @brief 文字と置き文字を出し分ける
        void Start() override;

        /// @brief 打っていれば打った文字のまま終える
        void OnDisable() override;

        /// @brief 打つのをやめ、重ねて描く物を消す
        void OnDestroy() override;

        /// @brief 届いた文字とキーで文字を直し、カーソルと選んだ範囲を描き直す
        void LateUpdate() override;

        /// @brief プロパティが変わったら、出す文字と見た目を合わせる
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        // ===== 問い合わせ =====

        /// @brief 文字が変わったか（変わったフレームの次のフレームだけ true）
        bool WasChanged() const;

        /// @brief 確定したか（確定したフレームの次のフレームだけ true）
        /// @details 1 行は Enter、複数行は Ctrl+Enter か、欄の外を押して打ち終えたとき。
        bool WasSubmitted() const;

        /// @brief 打っている最中か
        bool IsEditing() const { return editing_; }

        // ===== 文字 =====

        /// @brief 今の文字（打っている間は打ちかけの文字）
        const std::string& GetText() const { return text_; }

        /// @brief 文字を置き換える（中身の種類に合わない文字は捨て、最大の文字数で切る）
        void SetText(const std::string& text);

        /// @brief 最大の文字数（0 なら決めない）
        int GetCharacterLimit() const { return characterLimit_; }
        void SetCharacterLimit(int limit);

        /// @brief 中身の種類
        UIInputContentType GetContentType() const { return contentType_; }
        void SetContentType(UIInputContentType type);

        /// @brief 複数行か（Enter で改行し、長い文は折り返す）
        bool IsMultiLine() const { return multiLine_; }
        void SetMultiLine(bool multiLine);

        /// @brief 読み取りだけか（選ぶ・コピーはできるが、文字は変えられない）
        bool IsReadOnly() const { return readOnly_; }
        void SetReadOnly(bool readOnly);

        // ===== 繋ぎ =====

        /// @brief 打った文字を出す UI テキスト
        UITextComponent* GetTextComponent() const { return textComponent_.Get(); }
        void SetTextComponent(UITextComponent* text);

        /// @brief 空のときに出す置き文字
        UITextComponent* GetPlaceholder() const { return placeholder_.Get(); }
        void SetPlaceholder(UITextComponent* placeholder);

        // ===== 打つ =====

        /// @brief 打ち始める（カーソルは末尾）
        void Focus();

        /// @brief 打った文字のまま打ち終える
        void Unfocus() { EndEditing(false); }

        /// @brief 打ち終える
        /// @param cancel true なら打ち始める前の文字へ戻す
        void EndEditing(bool cancel);

        // ===== IUIInteractable =====

        bool AcceptsInput() const override { return IsInteractable() && IsEnabled(); }

        /// @brief 押した（ポインタなら、次の `OnDrag` で打ち始めてカーソルを置く）
        void OnPressBegin() override;

        /// @brief 押していたものを離した（決定キーなら打ち始める）
        void OnPressEnd(bool onSelf) override;

        /// @brief ポインタで押した所へカーソルを置き、押したまま動かした所まで選ぶ
        void OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize) override;

        /// @brief 押したまま動かしたら文字を選ぶので、スクロールに渡さない
        bool CapturesDrag() const override { return true; }

        /// @brief 打っている間は、向きの入力でフォーカスを動かさない
        bool OnNavigate(UINavigationDirection direction) override;

        bool IsEditingText() const override { return editing_; }
        void EndTextEditing(bool cancel) override { EndEditing(cancel); }

    protected:
        /// @brief 打っている間は選んでいるときの色にする
        void ApplyVisual() override;

    private:
        /// @brief 決定キーで押して離した（打ち始める）
        void OnClick() override;

        /// @brief 打ち始める（打ち始める前の文字を控え、文字の受け口を取る）
        /// @param refreshVisuals カーソルを末尾に置いた見た目まで直すか（押した所へ置き直す呼び手は false にして、ずらす前の文字で測る）
        void BeginEditing(bool refreshVisuals = true);

        /// @brief 打つのをやめる
        /// @param restore 打ち始める前の文字へ戻すか
        /// @param submitted 確定したことにするか
        void FinishEditing(bool restore, bool submitted);

        // ----- 文字を直す -----

        /// @brief 届いた文字とキーを順に当てる
        void ApplyEvents(const std::vector<TextInputEvent>& events);

        /// @brief 編集のキーを当てる
        void ApplyKey(const TextInputEvent& event);

        /// @brief カーソルの所へ文字を入れる（選んだ範囲は消す）。合わない文字は捨て、最大の文字数で止める
        /// @return 文字が変わったか
        bool InsertText(const std::u32string& text);

        /// @brief 選んだ範囲を消す
        /// @return 文字が変わったか
        bool DeleteSelection();

        /// @brief カーソルを動かす
        /// @param extend true なら選ぶ範囲を広げる（Shift を押しながら）
        void MoveCaret(int position, bool extend);

        /// @brief 選んでいる範囲があるか・その始めと終わり（文字の中の位置）
        bool HasSelection() const { return caret_ != anchor_; }
        int SelectionStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
        int SelectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }

        /// @brief 前・後ろの単語の切れ目
        int PreviousWordBoundary(int position) const;
        int NextWordBoundary(int position) const;

        /// @brief カーソルのある行の先頭・末尾（複数行のとき。折り返した行も 1 行と数える）
        int LineStart(int position);
        int LineEnd(int position);

        /// @brief 上・下の行の、今の横の位置にいちばん近い所へ動かす
        void MoveVertical(int direction, bool extend);

        /// @brief 選んだ範囲の文字をクリップボードへ書く
        void CopySelection() const;

        /// @brief 文字を直した後に、控えの文字と変わったフレームを合わせる
        void OnValueEdited();

        // ----- 文字の種類 -----

        /// @brief position の所へ入れてよい文字か
        bool IsAllowed(char32_t character, int position, const std::u32string& current) const;

        /// @brief 中身の種類と最大の文字数に合わせる
        std::u32string Sanitize(const std::u32string& text) const;

        /// @brief 今の文字を中身の種類と最大の文字数に合わせ直す
        void Revalidate();

        // ----- 出す -----

        /// @brief UI テキストへ出す文字（伏せ字は *、打っている間は変換中の文字も入れる）
        std::u32string BuildDisplay() const;

        /// @brief 出す文字と置き文字を合わせる
        void RefreshDisplay();

        /// @brief 出す文字の、文字の間ごとの位置を測る
        /// @param force false なら、出す文字が前に測ったときと同じ間は測り直さない
        bool EnsureCaretLayout(bool force = false);

        /// @brief 1 行・複数行に合わせて、文字と置き文字の置き方と折り返しを変える
        void ApplyLineLayout();

        /// @brief 出す文字の中の位置を、文字の中の位置へ直す
        int DisplayToValue(int displayIndex) const;

        /// @brief 文字の中の位置を、出す文字の中の位置へ直す
        int ValueToDisplay(int valueIndex) const;

        /// @brief UI テキストの座標の点に、いちばん近い文字の間（出す文字の中の位置）
        int HitTest(const Vector2& local) const;

        /// @brief カーソルが欄の中に入るよう、文字をずらす
        void ScrollToCaret();

        /// @brief カーソル・選んだ範囲・下線を描き直し、変換の窓をカーソルへ寄せる
        void RefreshVisuals();

        /// @brief 重ねて描く物を隠す
        void HideVisuals();

        /// @brief 重ねて描く物を消す
        void DestroyVisuals();

        /// @brief 重ねて描く物の根（選んだ範囲は文字の後ろ、カーソルと下線は文字の前）を作る
        bool EnsureVisualRoots(UITextComponent& text);

        /// @brief 根の下の index 番目の四角（足りなければ作る）
        UIImageComponent* QuadAt(RectTransformComponent& root, std::vector<ObjectRef<UIImageComponent>>& quads,
                                 std::size_t index, const char* name);

        /// @brief 変換の窓を寄せるカーソルの四角を、文字の受け口へ教える
        void PlaceImeCaret(const RectTransformComponent& textRect, const Vector2& caretLocal, float height) const;

        // ===== 保存する値 =====
        std::string text_;
        int characterLimit_ = 0;
        UIInputContentType contentType_ = UIInputContentType::Standard;
        bool multiLine_ = false;
        bool readOnly_ = false;
        ObjectRef<UITextComponent> textComponent_;
        ObjectRef<UITextComponent> placeholder_;
        Vector4 caretColor_ = { 0.20f, 0.20f, 0.20f, 1.0f };
        Vector4 selectionColor_ = { 0.66f, 0.81f, 1.0f, 0.75f };

        // ===== 打っている間 =====
        bool started_ = false;
        bool editing_ = false;
        std::uint32_t captureToken_ = 0;
        /// 打ちかけの文字（コードポイント列）
        std::u32string value_;
        /// 打ち始める前の文字（Esc で戻す）
        std::string textBeforeEdit_;
        /// カーソルと、選ぶ範囲の反対の端（文字の中の位置）
        int caret_ = 0;
        int anchor_ = 0;
        /// 上下で動かすときに保つ横の位置（UI テキストの座標）
        float preferredX_ = 0.0f;
        bool hasPreferredX_ = false;
        /// 変換中の文字とその中のカーソル
        std::u32string composition_;
        int compositionCursor_ = 0;

        /// ポインタで押して、まだカーソルを置いていない
        bool pointerPressPending_ = false;
        /// ポインタで押したまま選んでいる
        bool pointerSelecting_ = false;

        /// カーソルを点滅させる時間
        float blinkTime_ = 0.0f;

        /// 文字をずらした量（1 行は横、複数行は縦）と、ずらす前の文字の位置
        Vector2 scroll_ = { 0.0f, 0.0f };
        Vector2 textBasePosition_ = { 0.0f, 0.0f };

        /// 出している文字の、文字の間ごとの位置
        UITextCaretLayout caretLayout_;
        std::u32string measuredDisplay_;
        bool caretLayoutValid_ = false;

        /// 重ねて描く物
        ObjectRef<RectTransformComponent> backRoot_;
        ObjectRef<RectTransformComponent> frontRoot_;
        std::vector<ObjectRef<UIImageComponent>> selectionQuads_;
        std::vector<ObjectRef<UIImageComponent>> underlineQuads_;
        std::vector<ObjectRef<UIImageComponent>> caretQuads_;
        std::vector<ObjectId> visualObjects_;

        /// 繋ぎが足りないことを知らせたか
        bool warned_ = false;

        /// 文字が変わった・確定したフレーム（0 は一度も無い）
        std::uint64_t changedFrame_ = 0;
        std::uint64_t submittedFrame_ = 0;
    };
}
