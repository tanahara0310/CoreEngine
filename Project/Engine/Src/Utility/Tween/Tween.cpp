#include "pch.h"
#include "Tween.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/Quaternion/Quaternion.h"
#include "Utility/Tween/TweenManager.h"
#include "WorldTransform/WorldTransform.h"

namespace CoreEngine
{
    namespace TweenDetail
    {
        template <>
        Quaternion TweenLerp<Quaternion>(const Quaternion& from, const Quaternion& to, float t)
        {
            return MathCore::QuaternionMath::Slerp(from, to, t);
        }

        // ──────────────────────────────────────────────────────────
        // TweenItem : delay / ループ / コールバックの共通処理
        // ──────────────────────────────────────────────────────────

        bool TweenItem::Advance(float scaledDelta, float unscaledDelta)
        {
            if (finished_) { return true; }

            float deltaTime = (settings.updateType == TweenUpdate::Unscaled)
                ? unscaledDelta : scaledDelta;
            if (deltaTime < 0.0f) { deltaTime = 0.0f; }

            // ── 開始遅延 ──────────────────────────────────────────
            if (delayElapsed_ < settings.delay) {
                delayElapsed_ += deltaTime;
                if (delayElapsed_ < settings.delay) { return false; }

                // 待ち終わった分の余りを本編へ回す
                deltaTime = delayElapsed_ - settings.delay;
            }

            // 開始値は「実際に動き出す瞬間」に読む。
            // 遅延中に対象が別の理由で動いていても、そこから繋がって見える。
            if (!started_) {
                started_ = true;
                OnStart();
            }

            // ── 本編（1 フレームで複数ループを消化することがある）──
            for (int cycle = 0; cycle < kMaxCyclesPerFrame; ++cycle) {
                float overflow = 0.0f;
                const bool cycleDone = AdvanceCycle(deltaTime, overflow);

                if (settings.onUpdate) { settings.onUpdate(Progress()); }

                if (!cycleDone) { return false; }

                ++completedCycles_;
                if (settings.onStepComplete) { settings.onStepComplete(); }

                const bool infinite = (settings.loopCount < 0);
                if (!infinite && completedCycles_ >= settings.loopCount) {
                    finished_ = true;
                    if (settings.onComplete) { settings.onComplete(); }
                    return true;
                }

                if (settings.loopType == TweenLoop::Yoyo) { reversed_ = !reversed_; }
                RestartCycle(reversed_);

                deltaTime = overflow;
                if (deltaTime <= 0.0f) { return false; }
            }

            // 上限に達した = 長さ 0 のループを無限に回している。
            // 焼き付きを避けるためここで打ち切り、次フレームへ回す。
            return false;
        }

        void TweenItem::CompleteImmediate()
        {
            if (finished_) { return; }

            delayElapsed_ = settings.delay;

            if (!started_) {
                started_ = true;
                OnStart();
            }

            FinishCycle();
            finished_ = true;

            if (settings.onStepComplete) { settings.onStepComplete(); }
            if (settings.onComplete) { settings.onComplete(); }
        }

        void TweenItem::ResetForReplay()
        {
            delayElapsed_ = 0.0f;
            completedCycles_ = 0;
            started_ = false;
            finished_ = false;
            reversed_ = false;
            RestartCycle(false);
        }

        // ──────────────────────────────────────────────────────────
        // IntervalItem / CallbackItem
        // ──────────────────────────────────────────────────────────

        bool IntervalItem::AdvanceCycle(float deltaTime, float& overflow)
        {
            elapsed_ += deltaTime;
            if (elapsed_ >= duration_) {
                overflow = elapsed_ - duration_;
                return true;
            }
            return false;
        }

        bool CallbackItem::AdvanceCycle(float deltaTime, float& overflow)
        {
            // 長さを持たないので、渡された時間はそのまま次へ回す
            overflow = deltaTime;

            if (!fired_) {
                fired_ = true;
                if (action_) { action_(); }
            }
            return true;
        }

        void CallbackItem::FinishCycle()
        {
            if (!fired_) {
                fired_ = true;
                if (action_) { action_(); }
            }
        }

        // ──────────────────────────────────────────────────────────
        // SequenceItem
        // ──────────────────────────────────────────────────────────

        void SequenceItem::AppendStep(std::unique_ptr<TweenItem> item)
        {
            if (!item) { return; }

            Step step;
            step.items.push_back(std::move(item));
            steps_.push_back(std::move(step));
        }

        void SequenceItem::JoinLastStep(std::unique_ptr<TweenItem> item)
        {
            if (!item) { return; }

            if (steps_.empty()) {
                AppendStep(std::move(item));
                return;
            }
            steps_.back().items.push_back(std::move(item));
        }

        float SequenceItem::Progress() const
        {
            if (steps_.empty()) { return 1.0f; }

            const float done = static_cast<float>(currentStep_);
            const float total = static_cast<float>(steps_.size());
            return (done < total) ? (done / total) : 1.0f;
        }

        bool SequenceItem::AdvanceCycle(float deltaTime, float& overflow)
        {
            overflow = 0.0f;
            float stepDelta = deltaTime;

            while (currentStep_ < steps_.size()) {
                Step& step = steps_[currentStep_];

                bool allDone = true;
                for (std::unique_ptr<TweenItem>& item : step.items) {
                    if (!item || item->IsFinished()) { continue; }

                    // 子は親の時間で動く（親が Unscaled なら子も Unscaled）
                    if (!item->Advance(stepDelta, stepDelta)) { allDone = false; }
                }

                if (!allDone) { return false; }

                ++currentStep_;

                // 余り時間は次のステップへ繰り越さない。
                // 代わりに 0 秒で進めることで、長さを持たないステップ（Callback など）は
                // 同じフレームでまとめて消化され、実尺のあるトゥイーンは開始値だけが適用される。
                stepDelta = 0.0f;
            }

            return true;
        }

        void SequenceItem::RestartCycle(bool reversed)
        {
            // シーケンスの逆再生には対応しないので Yoyo は Restart と同じ挙動になる
            reversed_ = reversed;
            currentStep_ = 0;

            for (Step& step : steps_) {
                for (std::unique_ptr<TweenItem>& item : step.items) {
                    if (item) { item->ResetForReplay(); }
                }
            }
        }

        void SequenceItem::FinishCycle()
        {
            while (currentStep_ < steps_.size()) {
                for (std::unique_ptr<TweenItem>& item : steps_[currentStep_].items) {
                    if (item && !item->IsFinished()) { item->CompleteImmediate(); }
                }
                ++currentStep_;
            }
        }

        // ──────────────────────────────────────────────────────────
        // マネージャへの入口（Tween.h から TweenManager.h を見せないための橋渡し）
        // ──────────────────────────────────────────────────────────

        std::pair<std::uint32_t, std::uint32_t> Register(std::unique_ptr<TweenItem> item)
        {
            return TweenManager::GetInstance().Add(std::move(item));
        }

        TweenItem* Resolve(std::uint32_t index, std::uint32_t generation)
        {
            return TweenManager::GetInstance().Resolve(index, generation);
        }

        std::unique_ptr<TweenItem> Detach(std::uint32_t index, std::uint32_t generation)
        {
            return TweenManager::GetInstance().Detach(index, generation);
        }
    }

    // ──────────────────────────────────────────────────────────────
    // TweenHandle
    // ──────────────────────────────────────────────────────────────

    bool TweenHandle::IsActive() const
    {
        return TweenDetail::Resolve(index_, generation_) != nullptr;
    }

    TweenHandle& TweenHandle::SetEase(EasingUtil::Type ease)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.ease = ease;
        }
        return *this;
    }

    TweenHandle& TweenHandle::SetDelay(float seconds)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.delay = (seconds > 0.0f) ? seconds : 0.0f;
        }
        return *this;
    }

    TweenHandle& TweenHandle::SetLoops(int count, TweenLoop type)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.loopCount = count;
            item->settings.loopType = type;
        }
        return *this;
    }

    TweenHandle& TweenHandle::SetLink(const GameObject* owner)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.link = owner;
        }
        return *this;
    }

    TweenHandle& TweenHandle::SetUpdateType(TweenUpdate type)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.updateType = type;
        }
        return *this;
    }

    TweenHandle& TweenHandle::SetId(std::string id)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.id = std::move(id);
        }
        return *this;
    }

    TweenHandle& TweenHandle::OnComplete(std::function<void()> callback)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.onComplete = std::move(callback);
        }
        return *this;
    }

    TweenHandle& TweenHandle::OnStepComplete(std::function<void()> callback)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.onStepComplete = std::move(callback);
        }
        return *this;
    }

    TweenHandle& TweenHandle::OnUpdate(std::function<void(float)> callback)
    {
        if (auto* item = TweenDetail::Resolve(index_, generation_)) {
            item->settings.onUpdate = std::move(callback);
        }
        return *this;
    }

    void TweenHandle::Kill(bool complete)
    {
        auto* item = TweenDetail::Resolve(index_, generation_);
        if (!item) { return; }

        // 実体の解放は TweenManager::Update の掃除に任せる。
        // ここで消すと、コールバックから自分自身を Kill したときに
        // 実行中のスタックの下が消えてしまう。
        if (complete) {
            item->CompleteImmediate();
        } else {
            item->KillSilently();
        }
    }

    float TweenHandle::Progress() const
    {
        const auto* item = TweenDetail::Resolve(index_, generation_);
        return item ? item->Progress() : 1.0f;
    }

    // ──────────────────────────────────────────────────────────────
    // TweenSequence
    // ──────────────────────────────────────────────────────────────

    TweenSequence::TweenSequence()
    {
        auto item = std::make_unique<TweenDetail::SequenceItem>();
        const auto id = TweenDetail::Register(std::move(item));
        handle_ = TweenHandle(id.first, id.second);
    }

    TweenDetail::SequenceItem* TweenSequence::Item() const
    {
        // Register したのは SequenceItem であり、世代番号が一致する限り同一の実体なので
        // ダウンキャストは常に安全
        return static_cast<TweenDetail::SequenceItem*>(
            TweenDetail::Resolve(handle_.Index(), handle_.Generation()));
    }

    TweenSequence& TweenSequence::Append(const TweenHandle& tween)
    {
        if (auto* sequence = Item()) {
            // 単体で再生中のトゥイーンをマネージャから取り上げ、シーケンスの持ち物にする
            if (auto item = TweenDetail::Detach(tween.Index(), tween.Generation())) {
                sequence->AppendStep(std::move(item));
            }
        }
        return *this;
    }

    TweenSequence& TweenSequence::Join(const TweenHandle& tween)
    {
        if (auto* sequence = Item()) {
            if (auto item = TweenDetail::Detach(tween.Index(), tween.Generation())) {
                sequence->JoinLastStep(std::move(item));
            }
        }
        return *this;
    }

    TweenSequence& TweenSequence::AppendInterval(float seconds)
    {
        if (auto* sequence = Item()) {
            sequence->AppendStep(std::make_unique<TweenDetail::IntervalItem>(seconds));
        }
        return *this;
    }

    TweenSequence& TweenSequence::AppendCallback(std::function<void()> action)
    {
        if (auto* sequence = Item()) {
            sequence->AppendStep(std::make_unique<TweenDetail::CallbackItem>(std::move(action)));
        }
        return *this;
    }

    TweenSequence& TweenSequence::SetLoops(int count, TweenLoop type)
    {
        handle_.SetLoops(count, type);
        return *this;
    }

    TweenSequence& TweenSequence::SetDelay(float seconds)
    {
        handle_.SetDelay(seconds);
        return *this;
    }

    TweenSequence& TweenSequence::SetLink(const GameObject* owner)
    {
        handle_.SetLink(owner);
        return *this;
    }

    TweenSequence& TweenSequence::SetUpdateType(TweenUpdate type)
    {
        handle_.SetUpdateType(type);
        return *this;
    }

    TweenSequence& TweenSequence::SetId(std::string id)
    {
        handle_.SetId(std::move(id));
        return *this;
    }

    TweenSequence& TweenSequence::OnComplete(std::function<void()> callback)
    {
        handle_.OnComplete(std::move(callback));
        return *this;
    }

    // ──────────────────────────────────────────────────────────────
    // 生成口
    // ──────────────────────────────────────────────────────────────

    namespace Tween
    {
        TweenHandle Delay(float seconds, std::function<void()> action)
        {
            auto item = std::make_unique<TweenDetail::IntervalItem>(seconds);
            item->settings.onComplete = std::move(action);

            const auto id = TweenDetail::Register(std::move(item));
            return TweenHandle(id.first, id.second);
        }

        /// @brief GameObject の TransformComponent を取り出す（無ければ nullptr）
        static WorldTransform* GetWorldTransform(GameObject* object)
        {
            if (object == nullptr) { return nullptr; }

            auto* transform = object->GetComponent<TransformComponent>();
            return transform ? &transform->Get() : nullptr;
        }

        TweenHandle MoveTo(GameObject* object, const Vector3& to, float duration)
        {
            WorldTransform* transform = GetWorldTransform(object);
            if (transform == nullptr) { return TweenHandle(); }

            return To<Vector3>(&transform->translate, to, duration).SetLink(object);
        }

        TweenHandle ScaleTo(GameObject* object, const Vector3& to, float duration)
        {
            WorldTransform* transform = GetWorldTransform(object);
            if (transform == nullptr) { return TweenHandle(); }

            return To<Vector3>(&transform->scale, to, duration).SetLink(object);
        }

        TweenHandle RotateTo(GameObject* object, const Vector3& to, float duration)
        {
            WorldTransform* transform = GetWorldTransform(object);
            if (transform == nullptr) { return TweenHandle(); }

            return To<Vector3>(&transform->rotate, to, duration).SetLink(object);
        }

        int KillById(const std::string& id, bool complete)
        {
            return TweenManager::GetInstance().KillById(id, complete);
        }

        int KillByLink(const GameObject* owner, bool complete)
        {
            return TweenManager::GetInstance().KillByLink(owner, complete);
        }
    }
}
