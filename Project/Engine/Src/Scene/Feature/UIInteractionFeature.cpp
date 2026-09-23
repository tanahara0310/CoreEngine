#include "pch.h"
#include "Scene/Feature/UIInteractionFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderPassType.h"
#include "UI/IUIInteractable.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIPointer.h"

namespace CoreEngine
{
    void UIInteractionFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        // 編集中に効かせると、置いている最中のボタンが押されてしまう
        const bool playing = PlaybackStateManager::GetInstance().IsPlaying();
        if (!playing) {
            if (wasPlaying_) {
                ClearAll();
                wasPlaying_ = false;
            }
            return;
        }
        wasPlaying_ = true;

        if (!ctx.gameObjectManager || !ctx.engine) {
            return;
        }
        // UIRenderer はサービスではなく RenderManager が持つ
        auto* const renderManager = ctx.engine->GetService<RenderManager>();
        auto* const renderer = renderManager
            ? dynamic_cast<UIRenderer*>(renderManager->GetRenderer(RenderPassType::UI))
            : nullptr;
        if (!renderer) {
            return;
        }

        const UIPointer& pointer = UIPointer::Get();
        const Vector2 canvasSize = renderer->GetScreenSize();
        const Vector2 onCanvas = pointer.ToCanvas(canvasSize);

        IUIInteractable* const under =
            pointer.IsOver() ? PickTopmost(ctx, canvasSize, onCanvas) : nullptr;

        // ---- 乗った・外れた ----
        if (under != hovered_) {
            if (hovered_) { hovered_->OnPointerExit(); }
            hovered_ = under;
            if (hovered_) { hovered_->OnPointerEnter(); }
        }

        // ---- 押した ----
        if (pointer.IsPressed() && hovered_) {
            pressed_ = hovered_;
            pressed_->OnPointerDown();
        }

        // ---- 離した ----
        if (pointer.IsReleased() && pressed_) {
            // 押し始めた相手の上で離したときだけクリックにする
            pressed_->OnPointerUp(pressed_ == hovered_);
            pressed_ = nullptr;
        }
    }

    IUIInteractable* UIInteractionFeature::PickTopmost(SceneContext& ctx,
                                                      const Vector2& canvasSize,
                                                      const Vector2& pointer) const
    {
        IUIInteractable* best = nullptr;
        int bestOrder = 0;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                if (!candidate.AcceptsPointer()) {
                    return;
                }
                const RectTransformComponent* const rect = candidate.GetRectTransform();
                if (!rect || !rect->GetLayout().ContainsPoint(pointer, canvasSize)) {
                    return;
                }
                // 描画順が大きいほど手前。同じなら後から見つかった方を手前として扱う
                const int order = candidate.GetPointerSortOrder();
                if (!best || order >= bestOrder) {
                    best = &candidate;
                    bestOrder = order;
                }
            });

        return best;
    }

    void UIInteractionFeature::ClearAll()
    {
        if (pressed_) {
            pressed_->OnPointerUp(false);
            pressed_ = nullptr;
        }
        if (hovered_) {
            hovered_->OnPointerExit();
            hovered_ = nullptr;
        }
    }

    void UIInteractionFeature::Finalize(SceneContext& ctx)
    {
        // シーンと一緒に消えるので、通知を出さずにポインタだけ切る
        (void)ctx;
        pressed_ = nullptr;
        hovered_ = nullptr;
        wasPlaying_ = false;
    }
}
