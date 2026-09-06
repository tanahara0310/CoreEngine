#pragma once

#include "Math/Easing/EasingUtil.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class GameObject;
    struct Quaternion;

    /// @brief ループの折り返し方
    enum class TweenLoop {
        Restart, ///< 毎回 from から やり直す
        Yoyo,    ///< from→to→from→to… と往復する
    };

    /// @brief どちらの時間で進めるか
    enum class TweenUpdate {
        Scaled,   ///< Time::DeltaTime()（ヒットストップ・スローの影響を受ける）
        Unscaled, ///< Time::UnscaledDeltaTime()（ポーズ中も動く。メニュー演出はこちら）
    };

    namespace TweenDetail
    {
        /// @brief 汎用の線形補間。Vector2 / Vector3 / Vector4 / float が対象
        template <typename T>
        T TweenLerp(const T& from, const T& to, float t)
        {
            return from + (to - from) * t;
        }

        /// @brief Quaternion は球面線形補間する（定義は Tween.cpp）
        template <>
        Quaternion TweenLerp<Quaternion>(const Quaternion& from, const Quaternion& to, float t);

        /// @brief 単体トゥイーンにもシーケンスにも共通する設定
        struct TweenSettings {
            EasingUtil::Type ease = EasingUtil::Type::Linear;
            float delay = 0.0f;
            int loopCount = 1; ///< -1 で無限ループ
            TweenLoop loopType = TweenLoop::Restart;
            TweenUpdate updateType = TweenUpdate::Scaled;

            /// @brief 生存を紐づける相手。破棄されると自動でキルされる
            /// @note ポインタ先の値を書き換えるトゥイーンでは実質必須。
            ///       未設定のまま対象が死ぬと解放済みメモリへ書き込む。
            const GameObject* link = nullptr;

            /// @brief 一括キル・デバッグ表示用の名前
            std::string id;

            std::function<void()> onComplete;     ///< 全ループ完了時に 1 回
            std::function<void()> onStepComplete; ///< 1 ループ終わるたび
            std::function<void(float)> onUpdate;  ///< 毎フレーム（引数は進捗 0..1）
        };

        /// @brief 実行単位の基底。単体トゥイーン・シーケンス・コールバックが派生する
        /// @details delay / ループ / コールバックの処理はここに集約し、
        ///          派生は「1 サイクル分をどう進めるか」だけを実装する。
        class TweenItem
        {
        public:
            virtual ~TweenItem() = default;

            /// @brief 1 フレーム進める
            /// @param scaledDelta Time::DeltaTime()
            /// @param unscaledDelta Time::UnscaledDeltaTime()
            /// @return 完了したら true（呼び出し側が破棄する）
            bool Advance(float scaledDelta, float unscaledDelta);

            /// @brief 残りを飛ばして最終状態にし、完了させる（onComplete も呼ぶ）
            void CompleteImmediate();

            /// @brief 値を書き戻さずに終了させる
            void KillSilently() { finished_ = true; }

            /// @brief 最初から再生できる状態へ戻す
            /// @note Sequence がループするときに、抱えている子を巻き戻すために使う。
            void ResetForReplay();

            /// @brief 完了済みか
            bool IsFinished() const { return finished_; }

            /// @brief 進捗 0..1（デバッグ表示用）
            virtual float Progress() const = 0;

            /// @brief 種別名（デバッグ表示用）
            virtual const char* Kind() const = 0;

            /// @brief 1 サイクルの長さ（秒）。不定なら負値
            virtual float CycleDuration() const = 0;

            TweenSettings settings;

        protected:
            /// @brief 1 サイクル分を進める
            /// @param deltaTime 進める時間
            /// @param overflow サイクルが終わったときに余った時間を返す
            /// @return サイクルが終わったら true
            virtual bool AdvanceCycle(float deltaTime, float& overflow) = 0;

            /// @brief 次のサイクルへ向けて巻き戻す
            /// @param reversed Yoyo で逆再生に入るなら true
            virtual void RestartCycle(bool reversed) = 0;

            /// @brief 現在のサイクルを終端まで進める（値の書き込みも行う）
            virtual void FinishCycle() = 0;

            /// @brief 初回の Advance で 1 度だけ呼ばれる（開始値の遅延取得用）
            virtual void OnStart() {}

            bool reversed_ = false; ///< Yoyo の復路か

        private:
            /// @brief 1 フレームで消化するサイクル数の上限
            /// @note duration 0 の無限ループが CPU を焼き切るのを防ぐ
            static constexpr int kMaxCyclesPerFrame = 64;

            float delayElapsed_ = 0.0f;
            int completedCycles_ = 0;
            bool started_ = false;
            bool finished_ = false;
        };

        /// @brief 値をひとつ補間するトゥイーン
        template <typename T>
        class ValueTween final : public TweenItem
        {
        public:
            ValueTween(std::function<T()> getter, std::function<void(const T&)> setter,
                T to, float duration)
                : getter_(std::move(getter)), setter_(std::move(setter))
                , to_(std::move(to)), duration_(duration) {}

            /// @brief 開始値を明示するコンストラクタ（getter を用意できない相手向け）
            ValueTween(T from, T to, std::function<void(const T&)> setter, float duration)
                : setter_(std::move(setter)), from_(std::move(from))
                , to_(std::move(to)), duration_(duration), hasExplicitFrom_(true) {}

            float Progress() const override
            {
                if (duration_ <= 0.0f) { return 1.0f; }
                const float t = elapsed_ / duration_;
                return (t < 1.0f) ? t : 1.0f;
            }

            const char* Kind() const override { return "Tween"; }
            float CycleDuration() const override { return duration_; }

        protected:
            void OnStart() override
            {
                // 開始値はここで初めて読む。ビルド時ではなく再生開始時の値を使うことで、
                // Sequence で同じ対象を連続して動かしても飛びが起きない。
                if (!hasExplicitFrom_ && getter_) {
                    from_ = getter_();
                }
            }

            bool AdvanceCycle(float deltaTime, float& overflow) override
            {
                elapsed_ += deltaTime;

                if (duration_ <= 0.0f) {
                    overflow = deltaTime;
                    Write(reversed_ ? 0.0f : 1.0f);
                    return true;
                }

                if (elapsed_ >= duration_) {
                    overflow = elapsed_ - duration_;
                    Write(reversed_ ? 0.0f : 1.0f);
                    return true;
                }

                const float raw = elapsed_ / duration_;
                Write(reversed_ ? (1.0f - raw) : raw);
                return false;
            }

            void RestartCycle(bool reversed) override
            {
                elapsed_ = 0.0f;
                reversed_ = reversed;
            }

            void FinishCycle() override
            {
                elapsed_ = duration_;
                Write(reversed_ ? 0.0f : 1.0f);
            }

        private:
            /// @brief 生の進捗にイージングを掛けて書き込む
            void Write(float rawT)
            {
                if (!setter_) { return; }
                const float eased = EasingUtil::Apply(rawT, settings.ease);
                setter_(TweenLerp<T>(from_, to_, eased));
            }

            std::function<T()> getter_;
            std::function<void(const T&)> setter_;
            T from_{};
            T to_{};
            float duration_ = 0.0f;
            float elapsed_ = 0.0f;
            bool hasExplicitFrom_ = false;
        };

        /// @brief 時間を消費するだけの空トゥイーン（Sequence の待ち時間）
        class IntervalItem final : public TweenItem
        {
        public:
            explicit IntervalItem(float duration) : duration_(duration) {}

            float Progress() const override
            {
                if (duration_ <= 0.0f) { return 1.0f; }
                const float t = elapsed_ / duration_;
                return (t < 1.0f) ? t : 1.0f;
            }
            const char* Kind() const override { return "Interval"; }
            float CycleDuration() const override { return duration_; }

        protected:
            bool AdvanceCycle(float deltaTime, float& overflow) override;
            void RestartCycle(bool reversed) override { elapsed_ = 0.0f; reversed_ = reversed; }
            void FinishCycle() override { elapsed_ = duration_; }

        private:
            float duration_ = 0.0f;
            float elapsed_ = 0.0f;
        };

        /// @brief その場で 1 回だけ関数を呼ぶ（Sequence の途中で SE を鳴らす等）
        class CallbackItem final : public TweenItem
        {
        public:
            explicit CallbackItem(std::function<void()> action) : action_(std::move(action)) {}

            float Progress() const override { return fired_ ? 1.0f : 0.0f; }
            const char* Kind() const override { return "Callback"; }
            float CycleDuration() const override { return 0.0f; }

        protected:
            bool AdvanceCycle(float deltaTime, float& overflow) override;
            void RestartCycle(bool reversed) override { fired_ = false; reversed_ = reversed; }
            void FinishCycle() override;

        private:
            std::function<void()> action_;
            bool fired_ = false;
        };

        /// @brief 複数のトゥイーンを順番／同時に流す
        /// @note 子の updateType はシーケンス側の設定に従う（親のスケールで統一される）。
        class SequenceItem final : public TweenItem
        {
        public:
            /// @brief 同時に走る 1 かたまり
            struct Step {
                std::vector<std::unique_ptr<TweenItem>> items;
            };

            /// @brief 新しいステップとして末尾に足す（前のステップの完了後に始まる）
            void AppendStep(std::unique_ptr<TweenItem> item);

            /// @brief 直前のステップに合流させる（同時に走る）
            void JoinLastStep(std::unique_ptr<TweenItem> item);

            /// @brief ステップ数
            std::size_t StepCount() const { return steps_.size(); }

            float Progress() const override;
            const char* Kind() const override { return "Sequence"; }
            float CycleDuration() const override { return -1.0f; } // 可変

        protected:
            bool AdvanceCycle(float deltaTime, float& overflow) override;
            void RestartCycle(bool reversed) override;
            void FinishCycle() override;

        private:
            std::vector<Step> steps_;
            std::size_t currentStep_ = 0;
        };

        /// @brief 生成した実行単位を TweenManager へ登録する
        /// @note Tween.h から TweenManager.h を参照せずに済ませるための入口。実体は Tween.cpp。
        std::pair<std::uint32_t, std::uint32_t> Register(std::unique_ptr<TweenItem> item);

        /// @brief ハンドルから実行単位を引く（無効なら nullptr）
        TweenItem* Resolve(std::uint32_t index, std::uint32_t generation);

        /// @brief 実行単位をマネージャから取り外して所有権を奪う（Sequence へ移すため）
        std::unique_ptr<TweenItem> Detach(std::uint32_t index, std::uint32_t generation);
    }

    /// @brief 再生中のトゥイーンを指すハンドル
    ///
    /// @details
    /// **`Subscription` と違い、破棄しても再生は止まりません。**
    /// トゥイーンは「投げっぱなし」で使うのが普通なので、ハンドルを捨てても最後まで再生されます。
    /// 途中で止めたいときだけ保持して `Kill()` を呼んでください。
    ///
    /// @note 完了済み・キル済みのハンドルに対する操作はすべて安全な空振りになります
    ///       （世代番号で判別しているため、使い回しによる誤爆も起きません）。
    class TweenHandle
    {
    public:
        TweenHandle() = default;
        TweenHandle(std::uint32_t index, std::uint32_t generation)
            : index_(index), generation_(generation) {}

        /// @brief まだ再生中か
        bool IsActive() const;

        // ── 設定（生成直後に繋げて呼ぶ）─────────────────────────

        /// @brief イージングを指定する
        TweenHandle& SetEase(EasingUtil::Type ease);

        /// @brief 再生開始を遅らせる（秒）
        TweenHandle& SetDelay(float seconds);

        /// @brief ループ回数と折り返し方（count に -1 で無限）
        TweenHandle& SetLoops(int count, TweenLoop type = TweenLoop::Restart);

        /// @brief 生存を GameObject に紐づける（破棄されたら自動でキル）
        /// @note ポインタ先を書き換えるトゥイーンでは必ず指定すること。
        TweenHandle& SetLink(const GameObject* owner);

        /// @brief スケール済み時間／実時間の切り替え
        TweenHandle& SetUpdateType(TweenUpdate type);

        /// @brief 一括キルとデバッグ表示に使う名前を付ける
        TweenHandle& SetId(std::string id);

        /// @brief 全ループ完了時に 1 回呼ばれる
        TweenHandle& OnComplete(std::function<void()> callback);

        /// @brief 1 ループ終わるたびに呼ばれる
        TweenHandle& OnStepComplete(std::function<void()> callback);

        /// @brief 毎フレーム呼ばれる（引数は進捗 0..1）
        TweenHandle& OnUpdate(std::function<void(float)> callback);

        // ── 操作 ───────────────────────────────────────────────

        /// @brief 停止する
        /// @param complete true なら最終値まで飛ばして onComplete も呼ぶ
        void Kill(bool complete = false);

        /// @brief 最終値まで飛ばして完了させる
        void Complete() { Kill(true); }

        /// @brief 進捗 0..1（完了済みなら 1）
        float Progress() const;

        /// @brief 内部インデックス（Sequence への受け渡し用）
        std::uint32_t Index() const { return index_; }
        /// @brief 内部世代番号（Sequence への受け渡し用）
        std::uint32_t Generation() const { return generation_; }

    private:
        std::uint32_t index_ = 0;
        std::uint32_t generation_ = 0; ///< 0 は無効
    };

    /// @brief 複数のトゥイーンを順番／同時に組み立てるビルダー
    ///
    /// @details
    /// @code
    /// TweenSequence()
    ///     .Append(Tween::To(&alpha, 1.0f, 0.5f))
    ///     .Join  (Tween::To(&scale, 1.2f, 0.5f))   // 直前と同時
    ///     .AppendInterval(1.0f)
    ///     .AppendCallback([]{ Sound::Play("close"); })
    ///     .Append(Tween::To(&alpha, 0.0f, 0.3f))
    ///     .SetLink(gameObject);
    /// @endcode
    ///
    /// @note Append / Join に渡したトゥイーンは、マネージャから取り上げてシーケンスの
    ///       持ち物になります（単独では再生されなくなります）。
    class TweenSequence
    {
    public:
        TweenSequence();

        /// @brief 前のステップの完了後に始まるトゥイーンを足す
        TweenSequence& Append(const TweenHandle& tween);

        /// @brief 直前のステップと同時に走るトゥイーンを足す
        TweenSequence& Join(const TweenHandle& tween);

        /// @brief 待ち時間を挟む（秒）
        TweenSequence& AppendInterval(float seconds);

        /// @brief その場で 1 回呼ばれる処理を挟む
        TweenSequence& AppendCallback(std::function<void()> action);

        /// @brief シーケンス自身のハンドルを得る（設定・キルに使う）
        TweenHandle Handle() const { return handle_; }

        // TweenHandle と同じ設定を直接書けるようにする（末尾に繋げられる）
        TweenSequence& SetLoops(int count, TweenLoop type = TweenLoop::Restart);
        TweenSequence& SetDelay(float seconds);
        TweenSequence& SetLink(const GameObject* owner);
        TweenSequence& SetUpdateType(TweenUpdate type);
        TweenSequence& SetId(std::string id);
        TweenSequence& OnComplete(std::function<void()> callback);

        /// @brief 停止する
        void Kill(bool complete = false) { handle_.Kill(complete); }

    private:
        /// @brief ハンドルからシーケンス本体を引く（無効なら nullptr）
        TweenDetail::SequenceItem* Item() const;

        TweenHandle handle_;
    };

    /// @brief トゥイーンの生成口
    namespace Tween
    {
        /// @brief ポインタが指す値を to まで補間する
        /// @param target 書き換える変数のアドレス（トゥイーンの寿命中は生きている必要がある）
        /// @param to 目標値
        /// @param duration 所要時間（秒）
        /// @return 生成されたトゥイーンのハンドル（そのまま再生が始まる）
        /// @note **対象を持つ GameObject を `SetLink()` で紐づけること。**
        ///       紐づけないまま対象が破棄されると解放済みメモリへ書き込む。
        template <typename T>
        TweenHandle To(T* target, T to, float duration)
        {
            if (target == nullptr) { return TweenHandle(); }

            auto item = std::make_unique<TweenDetail::ValueTween<T>>(
                [target]() { return *target; },
                [target](const T& value) { *target = value; },
                std::move(to), duration);

            const auto id = TweenDetail::Register(std::move(item));
            return TweenHandle(id.first, id.second);
        }

        /// @brief セッター経由で from から to まで補間する
        /// @details `SetColor()` のように値を直接持てない相手に使う。
        /// @code
        /// Tween::To<Vector4>(baseColor, hitColor, 0.05f,
        ///     [material](const Vector4& c) { material->SetColor(c); }).SetLink(owner);
        /// @endcode
        template <typename T>
        TweenHandle To(T from, T to, float duration, std::function<void(const T&)> setter)
        {
            if (!setter) { return TweenHandle(); }

            auto item = std::make_unique<TweenDetail::ValueTween<T>>(
                std::move(from), std::move(to), std::move(setter), duration);

            const auto id = TweenDetail::Register(std::move(item));
            return TweenHandle(id.first, id.second);
        }

        /// @brief 指定秒後に 1 回だけ処理を呼ぶ
        /// @details 手書きのカウントダウン変数を置き換える用途。
        /// @code
        /// Tween::Delay(0.5f, [this]{ Explode(); }).SetLink(GetOwner());
        /// @endcode
        TweenHandle Delay(float seconds, std::function<void()> action);

        // ── Transform 向けの近道（対象の GameObject が自動で link される）──

        /// @brief 位置を動かす
        TweenHandle MoveTo(GameObject* object, const Vector3& to, float duration);

        /// @brief スケールを変える
        TweenHandle ScaleTo(GameObject* object, const Vector3& to, float duration);

        /// @brief オイラー角（ラジアン）で回す
        TweenHandle RotateTo(GameObject* object, const Vector3& to, float duration);

        // ── 一括操作 ───────────────────────────────────────────

        /// @brief SetId() で付けた名前が一致するトゥイーンをすべて止める
        /// @return 止めた本数
        int KillById(const std::string& id, bool complete = false);

        /// @brief 指定 GameObject に link されたトゥイーンをすべて止める
        /// @return 止めた本数
        int KillByLink(const GameObject* owner, bool complete = false);
    }
}
