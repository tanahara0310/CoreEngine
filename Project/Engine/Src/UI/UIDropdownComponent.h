#pragma once

#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/ObjectId.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UIElement.h"
#include "UI/UISelectableComponent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class GameObjectManager;
    class RectTransformComponent;
    class UIDropdownItemComponent;
    class UIScrollViewComponent;
    class UITextComponent;

    /// @brief 押すと選択肢の一覧が開き、1 つ選ぶ部品
    /// @details 押す所（このコンポーネント・今の値の文字・矢印）と、子に置いた一覧のひな形で組む。
    ///          一覧のひな形は再生中ふだんは隠し、開くと選択肢ごとに行のひな形を複製して並べ、
    ///          ほかの UI の上に出す（親のマスクにも切られない）。
    ///          一覧が画面の下にはみ出すなら上へ開き、行が少なければ一覧を行の分まで縮める。
    ///          行を選ぶと値が変わって閉じる。一覧の外を押す・戻る（`UICancel`）でも閉じる。
    /// @note 開いている間、ほかの UI は押せない（`UIInteractionFeature` が一覧の中だけに配る）。
    ///       選んだ値は `WasChanged()` で問い合わせる（スクリプトは `Update()` で見る）。
    class UIDropdownComponent final : public UISelectableComponent
    {
    public:
        UIDropdownComponent();

        const char* GetTypeName() const override { return "UIDropdown"; }

        REFLECT_DECLARE(UIDropdownComponent)

        /// @brief 一覧のひな形と行のひな形を隠し、今の値の文字を合わせる
        void Start() override;

        /// @brief 開いていれば閉じる
        void OnDisable() override;

        /// @brief 開いている間に押せなくなったら閉じる
        void LateUpdate() override;

        // ===== 問い合わせ =====

        /// @brief 値が変わったか
        /// @details 値が変わったフレームの次のフレームだけ true を返す。
        bool WasChanged() const;

        /// @brief 一覧が開いているか
        bool IsOpen() const { return open_; }

        // ===== 選択肢 =====

        const std::vector<std::string>& GetOptions() const { return options_; }

        /// @brief 選択肢を置き換える（今の値は選択肢の数に収める）
        void SetOptions(const std::vector<std::string>& options);

        /// @brief 型記述子とやり取りする選択肢（文字の配列）
        Reflection::ArrayValue GetOptionsValue() const;
        void SetOptionsValue(const Reflection::ArrayValue& value);

        /// @brief 選択肢を末尾に足す
        void AddOption(const std::string& text);

        /// @brief index 番目の選択肢を消す（今の値は同じ選択肢を指し続ける。消した選択肢なら前の選択肢へ）
        void RemoveOption(int index);

        /// @brief 選択肢を全部消し、今の値を 0 にする
        void ClearOptions();

        // ===== 値 =====

        /// @brief 今の値（選んでいる選択肢の番号）
        int GetValue() const { return value_; }

        /// @brief 今の値を変える（選択肢の数に収める）
        void SetValue(int value);

        // ===== 繋ぎ =====

        /// @brief 今の値を出す文字・一覧のひな形・行のひな形と行の文字を繋ぐ
        void SetCaptionText(UITextComponent* caption);
        void SetTemplate(RectTransformComponent* list);
        void SetItem(UIDropdownItemComponent* item, UITextComponent* itemText);

        // ===== 開け閉め =====

        /// @brief 一覧を開く（ほかのドロップダウンの一覧は閉じる）
        void Open();

        /// @brief 一覧を閉じる
        void Close();

        /// @brief 一覧の行が選ばれた（値を変えて閉じる）
        void ChooseFromList(int index);

        /// @brief 開いたばかりの一覧の大きさ・向き・スクロールを決める
        /// @note UI を並べ終えてから、描く順を決める前に呼ぶ。並べた後の行の位置と中身の大きさを使う。
        void PlaceListIfPending();

        // ===== IUIInteractable =====

        bool AcceptsInput() const override { return IsInteractable() && IsEnabled(); }
        RectTransformComponent* GetOpenPopup() const override;
        IUIInteractable* GetPopupInitialFocus() const override;
        bool IsPopupFocusTarget(const IUIInteractable& candidate) const override;
        void ClosePopup() override { Close(); }

    private:
        /// @brief 閉じていれば開き、開いていれば閉じる
        void OnClick() override;

        /// @brief 今の値の選択肢を、今の値を出す文字へ書く
        void RefreshCaption();

        /// @brief 今の値の行にだけ印を出す
        void RefreshSelection();

        /// @brief 選択肢ごとに行のひな形を複製して並べる
        void BuildRows();

        /// @brief 複製した行を消す
        void DestroyRows();

        /// @brief index 番目の行（無ければ nullptr）
        UIDropdownItemComponent* FindRow(int index) const;

        /// @brief 一覧の中のスクロール（ひな形か、行のひな形の親のどれかに付いているもの）
        UIScrollViewComponent* FindListScroll() const;

        /// @brief 今の値を選択肢の数に収める
        void ClampValue();

        std::vector<std::string> options_;
        int value_ = 0;
        ObjectRef<UITextComponent> captionText_;
        ObjectRef<RectTransformComponent> template_;
        ObjectRef<UIDropdownItemComponent> item_;
        ObjectRef<UITextComponent> itemText_;

        bool started_ = false;
        bool open_ = false;
        bool placePending_ = false;
        bool rowsDirty_ = true;

        /// 開いている一覧と、開く前の配置（閉じるときに戻す）
        ObjectRef<RectTransformComponent> openList_;
        UILayout listLayout_{};

        /// 複製した行と、行ごとに複製したオブジェクトの全部
        std::vector<ObjectRef<UIDropdownItemComponent>> rows_;
        std::vector<ObjectId> rowObjects_;

        /// 値が変わったフレーム（0 は一度も変わっていない）
        std::uint64_t changedFrame_ = 0;

        /// 繋ぎが足りないことを知らせたか
        bool warned_ = false;
    };

    /// @brief 開いたばかりのドロップダウンの一覧を置く
    /// @note UI を並べ終えてから、描く順を決める前に呼ぶ。開いたものが無ければ何もしない。
    void PlaceOpenDropdownLists(GameObjectManager& manager);
}
