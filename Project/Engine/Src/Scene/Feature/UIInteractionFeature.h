#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector2.h"
#include "UI/UINavigation.h"

#include <vector>

namespace CoreEngine
{
    class EngineSystem;
    class IUIInteractable;
    class InputQuery;
    class RectTransformComponent;
    class UIPointer;
    class UIScrollViewComponent;

    /// @brief ポインタとキー・パッドで UI を操作する Feature
    /// @details 毎フレーム、ポインタが指している相手と、キー・パッドで選んでいる相手（フォーカス）を
    ///          決めて配る。押せる UI は `IUIInteractable` を実装して受け取る。
    ///          ポインタで押したときも、フォーカス中に決定キーを押したときも、同じ口へ届く。
    /// @note 再生中だけ動かす。編集中に効かせると、置いている最中に押されてしまう。
    ///       一覧を開いている UI（ドロップダウン）があれば、ポインタ・スクロール・フォーカスはその一覧の中だけに配り、
    ///       一覧の外を押す・戻る（`UICancel`）で閉じさせる。
    class UIInteractionFeature : public ISceneFeature {
    public:
        /// @brief 今開いているシーンのものを引く
        /// @return 見つからなければ nullptr
        /// @note スクリプトからフォーカスを動かすときに使う。
        static UIInteractionFeature* FindCurrent(EngineSystem* engine);

        const char* GetName() const override { return "UIInteraction"; }

        /// @brief 送りと決定に使う入力アクションが揃っているかを確かめる
        void Initialize(SceneContext& ctx) override;

        /// @brief 指している相手とフォーカスを決め、状態を配る（PostObjectUpdate）
        /// @details オブジェクトの更新が終わってから見る。位置を動かした結果で当たりを取るため。
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        void Finalize(SceneContext& ctx) override;

        /// @brief いまポインタが指している相手（無ければ nullptr）
        IUIInteractable* GetHovered() const { return hovered_; }

        /// @brief いまキー・パッドで選んでいる相手（無ければ nullptr）
        IUIInteractable* GetFocused() const { return focused_; }

        /// @brief フォーカスを移す（同じ相手なら何もしない）
        /// @param next 選ぶ相手。nullptr で選択なしにする
        void SetFocus(IUIInteractable* next);

        /// @brief フォーカスを外す
        void ClearFocus() { SetFocus(nullptr); }

    private:
        /// @brief 覚えている相手がまだ居るか
        struct Presence {
            bool found = false;   ///< シーンに居て、動いている
            bool accepts = false; ///< 操作を受け付ける状態
        };

        /// @brief このフレームに見つけた、開いている一覧
        struct PopupScan {
            IUIInteractable* owner = nullptr;      ///< 一覧を開いている UI（無ければ nullptr）
            RectTransformComponent* root = nullptr; ///< その一覧
            Presence previous;                     ///< 前のフレームの持ち主がまだ居るか
        };

        /// @brief ポインタの下にあった押せる UI
        struct PointerHit {
            IUIInteractable* target = nullptr;
            const RectTransformComponent* rect = nullptr;
            int order = 0;
        };

        /// @brief 覚えている相手の生存を確かめつつ、ポインタの下の手前の 1 つを探す
        /// @param ctx シーン
        /// @param canvasSize UI の基準解像度
        /// @param pointer キャンバス座標でのポインタ
        /// @param pointerOver ポインタがゲーム画面の上にあるか
        /// @param outPopup 開いている一覧（一覧が開いていれば、その中の物だけを探す）
        /// @return ポインタの下にある、いちばん手前の押せる UI（無ければ nullptr）
        IUIInteractable* ScanAndPick(SceneContext& ctx, const Vector2& canvasSize,
                                     const Vector2& pointer, bool pointerOver, PopupScan& outPopup);

        /// @brief 消えた相手・受け付けなくなった相手を手放す
        void ReleaseLost(const Presence& hovered, const Presence& focused, const Presence& pressed);

        /// @brief 一覧が開いた・閉じたときにフォーカスを移す
        /// @details 開いたときにキー・パッドで選んでいたら一覧の最初の相手へ、
        ///          閉じたときに一覧の中をキー・パッドで選んでいたら一覧の持ち主へ戻す。
        void ChangePopup(SceneContext& ctx, const Vector2& canvasSize, const PopupScan& scan);

        /// @brief 一覧が開いている間、candidate が一覧の中にあってキー・パッドで選べるか（開いていなければ true）
        bool IsPopupFocusCandidate(const IUIInteractable& candidate) const;

        /// @brief 一覧を開いたときにフォーカスを置く相手
        IUIInteractable* FindPopupInitialFocus(SceneContext& ctx, const Vector2& canvasSize) const;

        /// @brief ポインタの下のいちばん手前のスクロールを探す（掴んでいるスクロールが消えていたら手放す）
        /// @return ポインタの下に無いか、スクロールの外の手前の UI に覆われていれば nullptr
        UIScrollViewComponent* PickScrollView(SceneContext& ctx, const Vector2& canvasSize,
                                              const Vector2& pointer, bool pointerOver);

        /// @brief ホイールとドラッグでスクロールを動かす
        /// @details 押して `UIScrollViewComponent::kDragThreshold` 動かすとドラッグにし、
        ///          押していたボタンは押されなかったことにする。
        void UpdateScroll(UIScrollViewComponent* under, const Vector2& canvasSize, const Vector2& onCanvas,
                          const UIPointer& pointer, const InputQuery* query);

        /// @brief 選んだ相手を含むスクロールを、相手が窓に入るまで動かす
        static void ScrollIntoView(const IUIInteractable& target);

        /// @brief キー・パッドでの送りを見る（押しっぱなしのくり返しもここ）
        void UpdateNavigation(SceneContext& ctx, const Vector2& canvasSize,
                              const InputQuery& query);

        /// @brief 押されている向きを 1 つ返す
        /// @return 何も押されていなければ false
        /// @note 上・下・左・右の順に見て、最初に押されていたものを採る。
        static bool ReadDirection(const InputQuery& query, UINavigationDirection& outDirection);

        /// @brief 向きの先にある相手へフォーカスを移す（見つからなければ動かさない）
        void MoveFocus(SceneContext& ctx, const Vector2& canvasSize,
                       UINavigationDirection direction);

        /// @brief まだ何も選んでいないときの送り先（いちばん左上のもの）
        IUIInteractable* FindFirstFocusable(SceneContext& ctx, const Vector2& canvasSize) const;

        /// @brief 覚えている相手をすべて手放し、通知を出す（再生をやめたとき）
        void ClearAll();

        /// @brief 入力の場面をゲームへ戻す（再生をやめたとき・シーンを閉じるとき）
        static void RestoreGameContext(SceneContext& ctx);

        // ポインタが指している相手（所有権は GameObjectManager）
        IUIInteractable* hovered_ = nullptr;

        // キー・パッドで選んでいる相手
        IUIInteractable* focused_ = nullptr;

        // 押している相手。離すまで追いかけ、同じ相手の上で離したときだけクリックにする
        IUIInteractable* pressed_ = nullptr;

        // 押し始めたのが決定キーか（false ならポインタ）。離したかを同じ入力で見るため
        bool pressedByKey_ = false;

        // ポインタで押したスクロールと、押した所（キャンバス座標）。離すまで持つ
        UIScrollViewComponent* scrollTarget_ = nullptr;
        Vector2 scrollPressPoint_{};
        // 押してから動かしてドラッグに切り替えたか
        bool scrollDragging_ = false;

        // 押しっぱなしで送り続けるための状態
        UINavigationDirection repeatDirection_ = UINavigationDirection::Up;
        bool repeatActive_ = false;
        float repeatTimer_ = 0.0f;

        // 一覧を開いている UI と、その一覧（無ければ nullptr）
        IUIInteractable* popupOwner_ = nullptr;
        RectTransformComponent* popupRoot_ = nullptr;
        // 一覧が開いている間にキー・パッドで選んでいたか（閉じたら持ち主へフォーカスを戻す）
        bool popupKeyboard_ = false;

        // ポインタの下にあった押せる UI（毎フレーム使い回す）
        std::vector<PointerHit> hits_;

        // 前のフレームに再生中だったか（やめた瞬間に 1 回だけ戻すため）
        bool wasPlaying_ = false;
    };
}
