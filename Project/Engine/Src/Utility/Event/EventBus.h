#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace CoreEngine
{
    /// @brief イベント型ごとに採番される連番 ID
    /// @note 採番順は「そのイベント型を最初に使った順」なので実行ごとに変わり得る。
    ///       保存・通信には使えない（プロセス内の索引専用）。
    using EventTypeId = std::uint32_t;

    /// @brief 購読 1 件を識別するハンドル（0 は無効値）
    using SubscriptionHandle = std::uint64_t;

    namespace EventDetail
    {
        /// @brief 新しいイベント型 ID を採番する
        /// @note 実体は EventBus.cpp。ヘッダ内 static にすると TU ごとに別 ID になるため、
        ///       採番元は必ず 1 か所に集める。
        EventTypeId AcquireEventTypeId();

        /// @brief イベント型 E に対応する ID を返す（型ごとに一度だけ採番される）
        template <typename E>
        EventTypeId TypeIdOf()
        {
            static const EventTypeId id = AcquireEventTypeId();
            return id;
        }

        /// @brief EventBus のシングルトンがまだ生きているか
        /// @note 静的破棄フェーズで Subscription のデストラクタが後から走るケースを弾く
        bool IsBusAlive();
    }

    class EventBus;

    /// @brief 購読の生存を握る RAII ハンドル
    /// @details デストラクタで自動的に購読解除する。ムーブのみ可能。
    ///          「購読したまま購読者が死ぬ」＝破棄済みオブジェクトのラムダが呼ばれる、という
    ///          このシステム最大の事故を型で防ぐのが目的なので、必ず変数に受けること。
    ///          Subscribe は [[nodiscard]] なので、受け忘れはコンパイル時に警告される。
    class Subscription
    {
    public:
        Subscription() = default;
        ~Subscription() { Unsubscribe(); }

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        Subscription(Subscription&& other) noexcept
            : typeId_(other.typeId_), handle_(other.handle_)
        {
            other.handle_ = 0;
        }

        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other) {
                Unsubscribe();
                typeId_ = other.typeId_;
                handle_ = other.handle_;
                other.handle_ = 0;
            }
            return *this;
        }

        /// @brief 明示的に購読解除する（以後 IsValid() は false）
        void Unsubscribe();

        /// @brief 購読中かどうか
        bool IsValid() const { return handle_ != 0; }

    private:
        friend class EventBus;

        Subscription(EventTypeId typeId, SubscriptionHandle handle)
            : typeId_(typeId), handle_(handle) {}

        EventTypeId typeId_ = 0;
        SubscriptionHandle handle_ = 0; // 0 = 無効
    };

    /// @brief 複数の購読をまとめて保持する入れ物
    /// @details 1 つのコンポーネントが複数種類のイベントを購読するときに、
    ///          Subscription をメンバに何本も並べる代わりに使う。破棄時に全件まとめて解除される。
    class SubscriptionBag
    {
    public:
        /// @brief 購読を預ける
        void Add(Subscription subscription)
        {
            subscriptions_.push_back(std::move(subscription));
        }

        /// @brief 預かっている購読をすべて解除する
        void Clear() { subscriptions_.clear(); }

        /// @brief 預かっている件数
        std::size_t Count() const { return subscriptions_.size(); }

    private:
        std::vector<Subscription> subscriptions_;
    };

    /// @brief 型安全なイベント配信のハブ（発行側と購読側を互いに知らせないための仲介役）
    ///
    /// @details
    /// 「敵が死んだ」という 1 つの事実に対して、スコア・SE・エフェクト・UI が
    /// それぞれ独立に反応できるようにする。発行側は誰が聞いているかを知らず、
    /// 購読側は誰が投げたかを知らない。結果として、機能の追加・削除が互いに影響しなくなる。
    ///
    /// @note **メインスレッド専用**。Publish / Queue / Subscribe / DispatchQueued は
    ///       すべてゲームループのスレッドから呼ぶこと。ロックを持たないのは、
    ///       Publish が毎フレーム数百回走るホットパスだから。
    ///       Debug ビルドでは別スレッドからの呼び出しを検出してログに出す。
    ///
    /// @note ハンドラ内から Publish / Subscribe / Unsubscribe を呼んでも安全。
    ///       配信中の Subscribe は配信完了後に反映され、Unsubscribe は即座に
    ///       「以後呼ばれない」が保証される（実体の除去だけが後回しになる）。
    class EventBus
    {
    public:
        /// @brief インスタンスを取得（シングルトンパターン）
        static EventBus& GetInstance();

        // ──────────────────────────────────────────────────────────
        // 購読
        // ──────────────────────────────────────────────────────────

        /// @brief イベント型 E の購読を開始する
        /// @tparam E イベント型（コピー / ムーブ可能な構造体）
        /// @param handler 受信ハンドラ
        /// @param priority 実行優先度。大きいほど先に呼ばれる（既定 0、同値なら登録順）
        /// @return 購読ハンドル。**破棄すると購読解除される**ので必ず保持すること
        /// @note priority は「スコアを加算してから UI がそれを読む」のような順序依存が
        ///       あるときにだけ使う。基本は既定値のままでよい。
        template <typename E>
        [[nodiscard]] Subscription Subscribe(std::function<void(const E&)> handler, int priority = 0)
        {
            static_assert(!std::is_reference_v<E>, "E must be a value type");
            if (!handler) { return Subscription(); }

            const EventTypeId typeId = EventDetail::TypeIdOf<E>();

            // 型消去した呼び出し口へ包む。ID と E の対応は TypeIdOf<E>() が保証するので
            // ここの static_cast は常に安全。
            auto invoker = [fn = std::move(handler)](const void* payload) {
                fn(*static_cast<const E*>(payload));
            };

            return SubscribeRaw(typeId, EventTypeName<E>(), std::move(invoker), priority);
        }

        // ──────────────────────────────────────────────────────────
        // 発行
        // ──────────────────────────────────────────────────────────

        /// @brief イベントを即座に配信する（呼び出し元のスタック上で全ハンドラが走る）
        /// @note 「弾が当たった → 敵を消す」のように、その場で結果が確定していてほしいものに使う。
        template <typename E>
        void Publish(const E& event)
        {
            PublishRaw(EventDetail::TypeIdOf<E>(), EventTypeName<E>(), &event);
        }

        /// @brief イベントをキューに積み、DispatchQueued() のタイミングでまとめて配信する
        /// @note UI 更新・SE・統計など「今フレーム中に届けば順序はどうでもよい」ものに使う。
        ///       ハンドラ内から更に Queue した分は**次フレーム**に回る（無限ループ防止）。
        template <typename E>
        void Queue(E event)
        {
            static_assert(std::is_move_constructible_v<E>, "E must be move-constructible");
            EnqueueRaw(std::make_unique<QueuedEvent<E>>(std::move(event)));
        }

        /// @brief 積まれたイベントをまとめて配信する
        /// @note エンジンが BaseScene::Update() から毎フレーム 1 回呼ぶ。利用側は呼ばなくてよい。
        void DispatchQueued();

        // ──────────────────────────────────────────────────────────
        // 管理
        // ──────────────────────────────────────────────────────────

        /// @brief 全購読と未配信キューを破棄する（シーン遷移時）
        /// @note 既存の Subscription は「解除済み」として無害化される（二重解除にならない）。
        ///       配信中に呼んではならない。
        void Clear();

        /// @brief イベント型 E の現在の購読者数
        template <typename E>
        std::size_t GetSubscriberCount() const
        {
            return GetSubscriberCountRaw(EventDetail::TypeIdOf<E>());
        }

#ifdef USE_IMGUI
        /// @brief デバッグパネルを描画する（Window > Analysis > Event Bus）
        /// @note 疎結合の代償は「何が飛んでいるか見えなくなること」。この画面がその代償を払う。
        void DrawImGui();
#endif

    private:
        EventBus();
        ~EventBus();
        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        friend class Subscription;

        /// @brief 型名を取得（デバッグ表示専用。RTTI 由来なので装飾が付くことがある）
        template <typename E>
        static const char* EventTypeName()
        {
            return typeid(E).name();
        }

        // ── キューに積むイベントの型消去 ──────────────────────────

        struct IQueuedEvent {
            virtual ~IQueuedEvent() = default;
            virtual void Dispatch(EventBus& bus) = 0;
        };

        template <typename E>
        struct QueuedEvent final : IQueuedEvent {
            explicit QueuedEvent(E value) : value_(std::move(value)) {}
            void Dispatch(EventBus& bus) override { bus.Publish(value_); }
            E value_;
        };

        // ── 購読 1 件 ─────────────────────────────────────────────

        struct Listener {
            SubscriptionHandle handle = 0;
            int priority = 0;
            bool alive = true; ///< 配信中の解除は実体を消さずここを倒す
            std::function<void(const void*)> invoke;
        };

        /// @brief イベント型 1 つ分の購読者リスト
        struct Channel {
            std::string name;                 ///< デバッグ表示用の型名
            std::vector<Listener> listeners;
            std::vector<Listener> pendingAdd; ///< 配信中に来た購読（配信後に合流させる）
            bool needsCompact = false;        ///< alive == false を含むか
#ifdef USE_IMGUI
            std::uint64_t publishTotal = 0;   ///< 起動からの累計発行回数
            std::uint32_t publishThisFrame = 0;
            std::uint32_t publishLastFrame = 0;
#endif
        };

        Subscription SubscribeRaw(EventTypeId typeId, const char* typeName,
            std::function<void(const void*)> invoker, int priority);
        void PublishRaw(EventTypeId typeId, const char* typeName, const void* payload);
        void EnqueueRaw(std::unique_ptr<IQueuedEvent> event);
        void UnsubscribeRaw(EventTypeId typeId, SubscriptionHandle handle);
        std::size_t GetSubscriberCountRaw(EventTypeId typeId) const;

        Channel& GetOrCreateChannel(EventTypeId typeId, const char* typeName);
        void FlushPendingChanges();

        /// @brief イベント型 ID で直接引く購読者リスト
        /// @note unique_ptr 越しにするのは、配信中のハンドラが「まだ誰も使っていないイベント型」を
        ///       購読して channels_ が再確保されても、配信中の Channel& が無効化されないため。
        std::vector<std::unique_ptr<Channel>> channels_;

        std::vector<std::unique_ptr<IQueuedEvent>> queued_;      ///< 次の DispatchQueued で配信する分
        std::vector<std::unique_ptr<IQueuedEvent>> dispatching_; ///< 配信中の作業用（再確保を避けて使い回す）

        SubscriptionHandle nextHandle_ = 1; ///< 0 は無効値なので 1 から
        int dispatchDepth_ = 0;             ///< 配信中の入れ子段数
        bool hasPendingChanges_ = false;    ///< 配信後に合流・圧縮が必要か

        static constexpr int kMaxDispatchDepth = 32; ///< 相互 Publish の無限再帰を止める閾値

#ifdef USE_IMGUI
        /// @brief 直近に流れたイベントの記録（デバッグパネル用のリングバッファ）
        struct TraceEntry {
            std::uint64_t frame = 0;
            std::string name;
            std::uint32_t listenerCount = 0;
            bool wasQueued = false;
        };

        static constexpr std::size_t kTraceCapacity = 256;

        std::vector<TraceEntry> trace_;
        std::size_t traceHead_ = 0;      ///< 次に書き込む位置
        std::size_t traceCount_ = 0;     ///< 有効な記録数（kTraceCapacity で頭打ち）
        bool tracePaused_ = false;
        bool dispatchingQueued_ = false; ///< 記録に「キュー経由」印を付けるため
        std::uint64_t statsFrame_ = 0;   ///< publishThisFrame を集計中のフレーム番号
        char traceFilter_[64] = {};

        void RecordTrace(const Channel& channel);
        void SyncFrameStats();
#endif
    };
}
