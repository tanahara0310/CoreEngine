#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    class IUIInteractable;

    /// @brief ポインタが指している UI を決め、押した・離したを配る Feature
    /// @details 毎フレーム、描画順の手前から当たりを取り、いちばん手前の 1 つだけへ配る。
    ///          ボタンのような「押せる UI」は `IUIInteractable` を実装して受け取る。
    /// @note 再生中だけ動かす。編集中に効かせると、置いている最中に押されてしまう。
    class UIInteractionFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "UIInteraction"; }

        /// @brief 指している相手を決め、状態を配る（PostObjectUpdate）
        /// @details オブジェクトの更新が終わってから見る。位置を動かした結果で当たりを取るため。
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        void Finalize(SceneContext& ctx) override;

        /// @brief いま指している相手（無ければ nullptr）
        IUIInteractable* GetHovered() const { return hovered_; }

    private:
        /// @brief ポインタの下にある、いちばん手前の押せる UI を探す
        /// @param ctx シーン
        /// @param canvasSize UI の基準解像度
        /// @param pointer キャンバス座標でのポインタ
        /// @return 見つからなければ nullptr
        IUIInteractable* PickTopmost(SceneContext& ctx, const Vector2& canvasSize,
                                     const Vector2& pointer) const;

        /// @brief 指している相手を手放し、通知を出す（再生をやめたとき）
        void ClearAll();

        // いま指している相手（所有権は GameObjectManager）
        IUIInteractable* hovered_ = nullptr;

        // 押し始めた相手。離すまで追いかけ、同じ相手の上で離したときだけクリックにする
        IUIInteractable* pressed_ = nullptr;

        // 前のフレームに再生中だったか（やめた瞬間に 1 回だけ戻すため）
        bool wasPlaying_ = false;
    };
}
