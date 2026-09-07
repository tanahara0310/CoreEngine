#pragma once

#include "Utility/Tween/Tween.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
    class GameObject;

    /// @brief 再生中のトゥイーンを保持して毎フレーム進めるマネージャ
    ///
    /// @details
    /// 利用側が直接触ることはほとんどない。`Tween::To()` 等が内部で登録し、
    /// `BaseScene::Update()` が `Update()` を、`BaseScene::Finalize()` が `Clear()` を呼ぶ。
    ///
    /// @note **メインスレッド専用**。トゥイーンは GameObject の値を書き換えるため、
    ///       ゲームループのスレッド以外から生成・更新してはいけない。
    class TweenManager
    {
    public:
        /// @brief インスタンスを取得（シングルトンパターン）
        static TweenManager& GetInstance();

        /// @brief 全トゥイーンを 1 フレーム進める
        /// @note link 先が破棄されたトゥイーンはここで自動的にキルされる。
        void Update();

        /// @brief 全トゥイーンを破棄する（シーン遷移時）
        /// @note 値の書き戻しは行わない。
        void Clear();

        /// @brief 実行単位を登録する
        /// @return {インデックス, 世代番号}。TweenHandle の中身になる
        std::pair<std::uint32_t, std::uint32_t> Add(std::unique_ptr<TweenDetail::TweenItem> item);

        /// @brief ハンドルから実行単位を引く
        /// @return 無効・完了済みなら nullptr
        TweenDetail::TweenItem* Resolve(std::uint32_t index, std::uint32_t generation);

        /// @brief 実行単位の所有権を奪って登録から外す（Sequence へ移すため）
        /// @return 無効な場合や、更新中で取り外せない場合は nullptr
        std::unique_ptr<TweenDetail::TweenItem> Detach(std::uint32_t index, std::uint32_t generation);

        /// @brief SetId() で付けた名前が一致するトゥイーンをすべて止める
        /// @return 止めた本数
        int KillById(const std::string& id, bool complete);

        /// @brief 指定 GameObject に link されたトゥイーンをすべて止める
        /// @return 止めた本数
        int KillByLink(const GameObject* owner, bool complete);

        /// @brief 再生中の本数
        std::size_t ActiveCount() const;

#ifdef USE_IMGUI
        /// @brief デバッグパネルを描画する（Window > Analysis > Tween）
        void DrawImGui();
#endif

    private:
        TweenManager() = default;
        ~TweenManager() = default;
        TweenManager(const TweenManager&) = delete;
        TweenManager& operator=(const TweenManager&) = delete;

        /// @brief 世代番号を進める（0 は無効値なので飛ばす）
        void BumpGeneration(std::size_t index);

        /// @brief スロットを空にして再利用待ちへ回す
        void ReleaseSlot(std::size_t index);

        struct Slot {
            std::unique_ptr<TweenDetail::TweenItem> item;
            std::uint32_t generation = 1; ///< 0 は無効値
        };

        std::vector<Slot> slots_;
        std::vector<std::uint32_t> freeList_;

        /// @brief Update 中に完了したスロット（走査を壊さないよう後でまとめて解放する）
        std::vector<std::uint32_t> pendingFree_;

        /// @brief いま Advance を実行中の実行単位
        /// @note そのスタックの下にある実体を Detach で持ち去られると解放済みメモリを踏むため、
        ///       この 1 本だけは取り外しを拒否する。
        TweenDetail::TweenItem* advancingItem_ = nullptr;

        bool updating_ = false;

#ifdef USE_IMGUI
        char filter_[64] = {};
#endif
    };
}
