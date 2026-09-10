#pragma once

#include "GameObject/Component/Core/IComponent.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <set>
#include <tuple>
#include <utility>

namespace GameComponents
{
    class GameManagerComponent;
    class MapGeneratorComponent;

    /// @brief バナナを1本収穫した瞬間の情報。演出だけに使う。
    /// @note 1匹のサルが1マス進むごとに、隣接するバナナの木の本数だけ発行される
    ///       （1マスの四方が木なら最大4本）。列車が長いほど同じ木で何度も発行されるので、
    ///       サルの数がそのまま「収穫が起きた回数」になる。
    struct BananaHarvestEvent {
        std::size_t monkeyIndex = 0;  ///< 収穫したサルの番号。0 が先頭
        std::int32_t treeGridX = 0;   ///< 収穫された木のマス
        std::int32_t treeGridZ = 0;
        std::int32_t monkeyGridX = 0; ///< そのサルがいるマス。木がどちらへしなるかに使う
        std::int32_t monkeyGridZ = 0;
        float amountRate = 1.0f;      ///< 1匹あたりの回復量が満額の何割か。サルが増えるほど小さい
        float amount = 0.0f;          ///< この木1本ぶんで実際に回復したスタミナ量
        std::size_t indexInFrame = 0; ///< 同じ瞬間に取れたうちの何本目か。0 始まり
    };

    /// @brief 建設に使うスタミナ、バナナ回復、サルによる消費倍率を管理する。
    class HungerComponent final : public CoreEngine::IComponent
    {
    public:
        explicit HungerComponent(
            MapGeneratorComponent* mapGenerator = nullptr,
            GameManagerComponent* gameManager = nullptr)
            : mapGenerator_(mapGenerator), gameManager_(gameManager) {}

        const char* GetTypeName() const override { return "Hunger"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "スタミナ"; }
        bool DrawInspector() override;
#endif

        void Start() override;
        void Update() override;

        /// @brief 先頭車両到着時の駅前レール・バナナ処理。未訪問の駅前ならtrueを返す。
        bool OnTrainEnteredCell(int32_t gridX, int32_t gridZ);
        /// @brief 各サルの通過時に共通スタミナを回復する。先頭のサルはindex=0。
        void OnMonkeyEnteredCell(std::size_t monkeyIndex, int32_t gridX, int32_t gridZ);
        /// @brief 駅から新しいトロッコが連結されるタイミングでサルを1匹追加する。
        void AddMonkey();
        /// @brief サル倍率込みの行動コストを求める。
        float CalculateActionCost(float baseAmount) const;
        /// @brief スタミナが足りる場合だけ消費する。
        bool TryConsumeStamina(float amount);
        /// @brief Undoなどでスタミナを回復する。
        void AddStamina(float amount);
        void SetMonkeyAddedCallback(std::function<void(std::size_t)> callback)
        {
            onMonkeyAdded_ = std::move(callback);
        }
        /// @brief サルを送り出した駅チップの位置を、サルが増えるたびに通知する。
        void SetStationPopCallback(std::function<void(int32_t, int32_t)> callback)
        {
            onStationPop_ = std::move(callback);
        }
        /// @brief 先頭が未訪問の駅前レールへ入った瞬間に、駅チップの位置を通知する。
        /// @details トロッコの速度が最低速度まで落ちるのはこの直後（TrainMovementComponent が
        ///          この関数の戻り値を見て落とす）なので、減速の演出はここで受けること。
        /// @note 連結（サルが増える）は最後尾が駅を抜けてからで、車両数ぶん遅れて起きる。
        ///       そちらは SetStationPopCallback / SetMonkeyAddedCallback で、
        ///       原因（減速）と結果（連結）が別の瞬間であることを承知のうえで使い分ける。
        void SetStationEnteredCallback(std::function<void(int32_t, int32_t)> callback)
        {
            onStationEntered_ = std::move(callback);
        }
        /// @brief バナナを1本収穫するたびに通知する。回復量の計算とは切り離した演出用の口。
        /// @note 通知先はスタミナを書き換えないこと。回復はこのコンポーネントが確定済み。
        void SetBananaHarvestCallback(std::function<void(const BananaHarvestEvent&)> callback)
        {
            onBananaHarvest_ = std::move(callback);
        }

        float GetCurrentHunger() const;
        float GetMaximumHunger() const;
        std::size_t GetMonkeyCount() const { return monkeyCount_; }
        float GetCostMultiplier() const;

    private:
        using BananaTriggerKey = std::tuple<std::size_t, int32_t, int32_t, int32_t, int32_t>;

        MapGeneratorComponent* mapGenerator_ = nullptr;
        GameManagerComponent* gameManager_ = nullptr;

        float currentHunger_ = 100.0f; // すべてのサルで共有するスタミナ。
        bool gameOverRequested_ = false;
        std::size_t monkeyCount_ = 1;
        std::function<void(std::size_t)> onMonkeyAdded_;
        std::function<void(int32_t, int32_t)> onStationPop_;
        std::function<void(int32_t, int32_t)> onStationEntered_;
        std::function<void(const BananaHarvestEvent&)> onBananaHarvest_;
        // 発動した駅チップの位置。サルが増えるたびに古い順へ取り出す
        std::deque<std::pair<int32_t, int32_t>> pendingMonkeyStations_;

        // サルごとに、木の座標と通った隣接マスの組につき一度だけ発動させる。
        std::set<BananaTriggerKey> activatedBananaSides_;
        std::set<std::pair<int32_t, int32_t>> activatedStations_;
    };
}
