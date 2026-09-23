#include "pch.h"
#include "Scene/Feature/UIInteractionFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderPassType.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "UI/IUIInteractable.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIPointer.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// 決定に使うアクションの綴り
        constexpr const char* kConfirmActionId = "UIConfirm";

        /// 送りに使うアクションの綴り（`UINavigationDirection` と同じ並び）
        constexpr const char* kDirectionActionIds[] = {
            "UINavigateUp",
            "UINavigateDown",
            "UINavigateLeft",
            "UINavigateRight",
        };
        static_assert(std::size(kDirectionActionIds) == 4, "向きの数と綴りの数を合わせること");

        /// 押しっぱなしのとき、最初の 1 回から次までの待ち時間［秒］
        constexpr float kFirstRepeatDelay = 0.45f;

        /// そのあと送り続ける間隔［秒］
        constexpr float kRepeatInterval = 0.12f;
    }

    UIInteractionFeature* UIInteractionFeature::FindCurrent(EngineSystem* engine)
    {
        SceneManager* const sceneManager = engine ? engine->GetService<SceneManager>() : nullptr;
        auto* const scene = dynamic_cast<Scene*>(sceneManager ? sceneManager->GetCurrentScene() : nullptr);
        return scene ? scene->GetFeature<UIInteractionFeature>() : nullptr;
    }

    void UIInteractionFeature::Initialize(SceneContext& ctx)
    {
        (void)ctx;
        // 綴りが表に無いとフォーカス送りが黙って効かなくなるので、シーンを開いた時点で知らせる
        for (const char* const id : kDirectionActionIds) {
            if (InputActionFromString(id) == InputAction::Invalid) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "入力アクション \"{}\" がありません。キー・パッドでの UI の送りは効きません", id);
            }
        }
        if (InputActionFromString(kConfirmActionId) == InputAction::Invalid) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "入力アクション \"{}\" がありません。キー・パッドでの UI の決定は効きません",
                kConfirmActionId);
        }
    }

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

        InputManager* const input = ctx.engine->GetService<InputManager>();
        const InputQuery* const query = input ? &input->GetQuery() : nullptr;
        const InputAction confirm = InputActionFromString(kConfirmActionId);
        const bool hasConfirm = query && confirm != InputAction::Invalid;
        const bool confirmDown = hasConfirm && query->IsActionTriggered(confirm);
        const bool confirmUp = hasConfirm && query->IsActionReleased(confirm);

        IUIInteractable* const under = ScanAndPick(ctx, canvasSize, onCanvas, pointer.IsOver());

        // ---- 乗った・外れた ----
        if (under != hovered_) {
            if (hovered_) { hovered_->OnPointerExit(); }
            hovered_ = under;
            if (hovered_) { hovered_->OnPointerEnter(); }
        }

        // ---- 押した ----
        if (!pressed_ && pointer.IsPressed() && hovered_) {
            pressed_ = hovered_;
            pressedByKey_ = false;
            pressed_->OnPressBegin();
        }
        if (!pressed_ && focused_ && confirmDown) {
            pressed_ = focused_;
            pressedByKey_ = true;
            pressed_->OnPressBegin();
        }

        // ---- 離した ----
        if (pressed_) {
            // 押し始めた入力で離したかを見る（マウスで押して決定キーを離す、の取り違えを防ぐ）
            const bool released = pressedByKey_ ? confirmUp : pointer.IsReleased();
            if (released) {
                // 押し始めた相手の上で離したときだけクリックにする
                const IUIInteractable* const on = pressedByKey_ ? focused_ : hovered_;
                pressed_->OnPressEnd(pressed_ == on);
                pressed_ = nullptr;
            }
        }

        // ---- キー・パッドでの送り ----
        if (query) {
            UpdateNavigation(ctx, canvasSize, *query);
        }
    }

    IUIInteractable* UIInteractionFeature::ScanAndPick(SceneContext& ctx, const Vector2& canvasSize,
                                                      const Vector2& pointer, bool pointerOver)
    {
        IUIInteractable* best = nullptr;
        int bestOrder = 0;
        Presence hovered;
        Presence focused;
        Presence pressed;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                const bool accepts = candidate.AcceptsInput();

                // 覚えている相手がまだ居るかをここで確かめる
                // （走査に出てこないものは、消えたか止まっている）
                const auto mark = [&](const IUIInteractable* known, Presence& presence) {
                    if (known == &candidate) {
                        presence.found = true;
                        presence.accepts = accepts;
                    }
                    };
                mark(hovered_, hovered);
                mark(focused_, focused);
                mark(pressed_, pressed);

                if (!accepts || !pointerOver) {
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

        ReleaseLost(hovered, focused, pressed);
        return best;
    }

    void UIInteractionFeature::ReleaseLost(const Presence& hovered, const Presence& focused,
                                           const Presence& pressed)
    {
        // 消えた相手には通知を出せない。受け付けなくなっただけの相手には 1 回だけ知らせる
        if (pressed_ && !pressed.found) {
            pressed_ = nullptr;
        }
        else if (pressed_ && !pressed.accepts) {
            pressed_->OnPressEnd(false);
            pressed_ = nullptr;
        }

        if (hovered_ && !hovered.found) {
            hovered_ = nullptr;
        }
        else if (hovered_ && !hovered.accepts) {
            hovered_->OnPointerExit();
            hovered_ = nullptr;
        }

        if (focused_ && !focused.found) {
            focused_ = nullptr;
        }
        else if (focused_ && !focused.accepts) {
            focused_->OnFocusExit();
            focused_ = nullptr;
        }
    }

    void UIInteractionFeature::UpdateNavigation(SceneContext& ctx, const Vector2& canvasSize,
                                                const InputQuery& query)
    {
        if (pressed_) {
            // 押している間は送らない。押しながら選び直すと、どこで離したか分からなくなる
            repeatActive_ = false;
            return;
        }

        UINavigationDirection direction = UINavigationDirection::Up;
        if (!ReadDirection(query, direction)) {
            repeatActive_ = false;
            return;
        }

        bool send = false;
        if (!repeatActive_ || direction != repeatDirection_) {
            // 押した瞬間と、向きを変えた瞬間は待たずに送る
            repeatActive_ = true;
            repeatDirection_ = direction;
            repeatTimer_ = kFirstRepeatDelay;
            send = true;
        }
        else {
            // ポーズ中でもメニューは動かせる必要があるので、止まらない方の時間で測る
            repeatTimer_ -= Time::UnscaledDeltaTime();
            if (repeatTimer_ <= 0.0f) {
                repeatTimer_ = kRepeatInterval;
                send = true;
            }
        }

        if (send) {
            MoveFocus(ctx, canvasSize, direction);
        }
    }

    bool UIInteractionFeature::ReadDirection(const InputQuery& query,
                                             UINavigationDirection& outDirection)
    {
        // アクションの表はエディタから変えられるので、綴りで引き直す
        for (std::size_t i = 0; i < std::size(kDirectionActionIds); ++i) {
            const InputAction action = InputActionFromString(kDirectionActionIds[i]);
            if (action == InputAction::Invalid) {
                continue;
            }
            if (query.IsActionPressed(action)) {
                outDirection = static_cast<UINavigationDirection>(i);
                return true;
            }
        }
        return false;
    }

    void UIInteractionFeature::MoveFocus(SceneContext& ctx, const Vector2& canvasSize,
                                         UINavigationDirection direction)
    {
        if (!focused_) {
            // まだどこも選んでいない。最初の入力では置くだけにする
            // （ポインタが指しているものがあれば、そこから続ける）
            SetFocus(hovered_ ? hovered_ : FindFirstFocusable(ctx, canvasSize));
            return;
        }

        const RectTransformComponent* const rect = focused_->GetRectTransform();
        if (!rect) {
            return;
        }
        const Vector2 dir = UINavigation::ToVector(direction);
        const Vector2 from = UINavigation::EdgePoint(rect->GetLayout().CalculateRect(canvasSize), dir);

        IUIInteractable* best = nullptr;
        float bestScore = 0.0f;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                if (&candidate == focused_ || !candidate.AcceptsInput()) {
                    return;
                }
                const RectTransformComponent* const candidateRect = candidate.GetRectTransform();
                if (!candidateRect) {
                    return;
                }
                const Vector2 center =
                    UINavigation::Center(candidateRect->GetLayout().CalculateRect(canvasSize));
                const float score = UINavigation::Score(from, dir, center);
                if (score > bestScore) {
                    bestScore = score;
                    best = &candidate;
                }
            });

        // 向きの先に何も無ければ動かさない（端で止まる）
        if (best) {
            SetFocus(best);
        }
    }

    IUIInteractable* UIInteractionFeature::FindFirstFocusable(SceneContext& ctx,
                                                             const Vector2& canvasSize) const
    {
        IUIInteractable* best = nullptr;
        float bestDistance = 0.0f;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                if (!candidate.AcceptsInput()) {
                    return;
                }
                const RectTransformComponent* const rect = candidate.GetRectTransform();
                if (!rect) {
                    return;
                }
                // 画面の左上にいちばん近いものから始める
                const Vector2 center = UINavigation::Center(rect->GetLayout().CalculateRect(canvasSize));
                const float distance = center.x + center.y;
                if (!best || distance < bestDistance) {
                    best = &candidate;
                    bestDistance = distance;
                }
            });

        return best;
    }

    void UIInteractionFeature::SetFocus(IUIInteractable* next)
    {
        if (focused_ == next) {
            return;
        }
        if (focused_) {
            focused_->OnFocusExit();
        }
        focused_ = next;
        if (focused_) {
            focused_->OnFocusEnter();
        }
    }

    void UIInteractionFeature::ClearAll()
    {
        if (pressed_) {
            pressed_->OnPressEnd(false);
            pressed_ = nullptr;
        }
        if (hovered_) {
            hovered_->OnPointerExit();
            hovered_ = nullptr;
        }
        SetFocus(nullptr);
        repeatActive_ = false;
    }

    void UIInteractionFeature::Finalize(SceneContext& ctx)
    {
        // シーンと一緒に消えるので、通知を出さずにポインタだけ切る
        (void)ctx;
        pressed_ = nullptr;
        hovered_ = nullptr;
        focused_ = nullptr;
        repeatActive_ = false;
        wasPlaying_ = false;
    }
}
