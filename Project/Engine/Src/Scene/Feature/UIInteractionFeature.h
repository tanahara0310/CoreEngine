#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector2.h"
#include "UI/UINavigation.h"

namespace CoreEngine
{
    class EngineSystem;
    class IUIInteractable;
    class InputQuery;

    /// @brief ポインタとキー・パッドで UI を操作する Feature
    /// @details 毎フレーム、ポインタが指している相手と、キー・パッドで選んでいる相手（フォーカス）を
    ///          決めて配る。押せる UI は `IUIInteractable` を実装して受け取る。
    ///          ポインタで押したときも、フォーカス中に決定キーを押したときも、同じ口へ届く。
    /// @note 再生中だけ動かす。編集中に効かせると、置いている最中に押されてしまう。
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

        /// @brief 覚えている相手の生存を確かめつつ、ポインタの下の手前の 1 つを探す
        /// @param ctx シーン
        /// @param canvasSize UI の基準解像度
        /// @param pointer キャンバス座標でのポインタ
        /// @param pointerOver ポインタがゲーム画面の上にあるか
        /// @return ポインタの下にある、いちばん手前の押せる UI（無ければ nullptr）
        IUIInteractable* ScanAndPick(SceneContext& ctx, const Vector2& canvasSize,
                                     const Vector2& pointer, bool pointerOver);

        /// @brief 消えた相手・受け付けなくなった相手を手放す
        void ReleaseLost(const Presence& hovered, const Presence& focused, const Presence& pressed);

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

        // ポインタが指している相手（所有権は GameObjectManager）
        IUIInteractable* hovered_ = nullptr;

        // キー・パッドで選んでいる相手
        IUIInteractable* focused_ = nullptr;

        // 押している相手。離すまで追いかけ、同じ相手の上で離したときだけクリックにする
        IUIInteractable* pressed_ = nullptr;

        // 押し始めたのが決定キーか（false ならポインタ）。離したかを同じ入力で見るため
        bool pressedByKey_ = false;

        // 押しっぱなしで送り続けるための状態
        UINavigationDirection repeatDirection_ = UINavigationDirection::Up;
        bool repeatActive_ = false;
        float repeatTimer_ = 0.0f;

        // 前のフレームに再生中だったか（やめた瞬間に 1 回だけ戻すため）
        bool wasPlaying_ = false;
    };
}
