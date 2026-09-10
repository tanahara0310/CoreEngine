#include "pch.h"
#include "BananaTreeAuraFeature.h"

#include "Components/Building/MapChipData.h"
#include "Components/Building/MapGeneratorComponent.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/Rail/RailPathComponent.h"
#include "Components/UI/PauseMenuUIComponent.h"
#include "Components/Utility/BlockModelLayout.h"

#include "GameObject/GameObjectManager.h"
#include "Graphics/Line/LineManager.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <unordered_set>
#include <utility>

using namespace CoreEngine;

namespace {

    CVar<bool> cvEnabled{
        "Game.BananaTreeAura.Enabled", true,
        "バナナの木の隣接マスに、収穫範囲を示す四角い波動を出す" };

    CVar<Vector4> cvColor{
        "Game.BananaTreeAura.Color", Vector4{ 1.0f, 0.86f, 0.18f, 1.0f },
        "四角い波動の色" };

    CVar<float> cvAlpha{
        "Game.BananaTreeAura.Alpha", 0.8f,
        "波動の最大不透明度",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvSpeed{
        "Game.BananaTreeAura.Speed", 0.75f,
        "波動が1秒間に外側へ広がる回数",
        CVarRange{ 0.0f, 5.0f } };

    CVar<int> cvWaveCount{
        "Game.BananaTreeAura.WaveCount", 3,
        "1マスに同時表示する波動の本数",
        CVarRange{ 1, 8 } };

    CVar<float> cvMinSize{
        "Game.BananaTreeAura.MinSize", 0.16f,
        "波動が現れるときの一辺（マスに対する割合）",
        CVarRange{ 0.01f, 1.0f } };

    CVar<float> cvMaxSize{
        "Game.BananaTreeAura.MaxSize", 0.88f,
        "波動が消えるときの一辺（マスに対する割合）",
        CVarRange{ 0.01f, 1.5f } };

    CVar<float> cvHeightOffset{
        "Game.BananaTreeAura.HeightOffset", 0.025f,
        "地面から波動を浮かせる高さ（マスに対する割合）",
        CVarRange{ 0.0f, 0.5f } };

    using Cell = std::pair<int32_t, int32_t>;

    constexpr std::array<Cell, 4> kAdjacentDirections{ {
        { +1,  0 },
        { -1,  0 },
        {  0, +1 },
        {  0, -1 },
    } };

    /// @brief 非負のグリッド座標を1つの整数へ詰める
    uint64_t CellKey(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
            static_cast<uint32_t>(z);
    }

    /// @brief 1本の四角い波動をXZ平面へ描く
    void DrawSquareWave(
        LineManager& lines,
        float centerX,
        float centerZ,
        float height,
        float sideLength,
        const Vector3& color,
        float alpha)
    {
        const float half = sideLength * 0.5f;
        const Vector3 corners[4]{
            { centerX - half, height, centerZ - half },
            { centerX + half, height, centerZ - half },
            { centerX + half, height, centerZ + half },
            { centerX - half, height, centerZ + half },
        };

        constexpr bool kDepthTest = true;
        for (std::size_t i = 0; i < 4; ++i) {
            lines.DrawLine(corners[i], corners[(i + 1) % 4], color, alpha, kDepthTest);
        }
    }

    class BananaTreeAuraFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "BananaTreeAura"; }

        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            mapGenerator_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::MapGeneratorComponent>();
            railPath_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::RailPathComponent>();

            if (!mapGenerator_ || !railPath_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "BananaTreeAuraFeature: 必要なコンポーネントが見つからないため波動を出しません");
            }
        }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override
        {
            // ポーズメニューは PauseMenuFeature が PostSceneInitialize で作る。
            // Feature の登録順に依存しないよう、全 Feature の初期化が終わった
            // 最初の Update で 1 度だけ引く。
            if (!pauseMenuResolved_) {
                if (ctx.gameObjectManager) {
                    pauseMenu_ = ctx.gameObjectManager
                        ->FindFirstComponent<GameComponents::PauseMenuUIComponent>();
                }
                pauseMenuResolved_ = true;
            }

            // RailBuilderComponent の更新後に判定し、レールを敷いたフレームですぐ消す。
            if (phase != SceneUpdatePhase::PostObjectUpdate ||
                !cvEnabled.Get() || !mapGenerator_ || !railPath_ ||
                (pauseMenu_ && pauseMenu_->IsVisible())) {
                return;
            }

            const float speed = std::max(0.0f, cvSpeed.Get());
            wavePhase_ = std::fmod(
                wavePhase_ + std::max(0.0f, Time::DeltaTime()) * speed,
                1.0f);

            std::unordered_set<uint64_t> railCells;
            const auto& confirmedRails = railPath_->GetRailMap();
            const auto& pendingRails = railPath_->GetRailUndoStack();
            railCells.reserve(confirmedRails.size() + pendingRails.size());
            for (const Cell& cell : confirmedRails) {
                railCells.insert(CellKey(cell.first, cell.second));
            }
            for (const Cell& cell : pendingRails) {
                railCells.insert(CellKey(cell.first, cell.second));
            }

            // 木が隣り合っている場合でも、同じマスへ波動を二重描画しない。
            std::unordered_set<uint64_t> auraCells;
            const auto& map = mapGenerator_->GetMapChips();
            for (std::size_t x = 0; x < map.size(); ++x) {
                for (std::size_t z = 0; z < map[x].size(); ++z) {
                    if (map[x][z] != GameComponents::MapChipType::BananaTree) {
                        continue;
                    }

                    for (const Cell& direction : kAdjacentDirections) {
                        const int32_t auraX = static_cast<int32_t>(x) + direction.first;
                        const int32_t auraZ = static_cast<int32_t>(z) + direction.second;
                        if (auraX < 0 || auraZ < 0 ||
                            auraX >= static_cast<int32_t>(map.size()) ||
                            auraZ >= static_cast<int32_t>(map[static_cast<std::size_t>(auraX)].size())) {
                            continue;
                        }

                        const auto cellX = static_cast<std::size_t>(auraX);
                        const auto cellZ = static_cast<std::size_t>(auraZ);
                        const auto chip = map[cellX][cellZ];
                        // 岩・駅・木など占有物があるマスには出さない。通常地面、
                        // 装飾だけの草地、水上の3種類だけが波動の対象になる。
                        const bool isOpenCell =
                            chip == GameComponents::MapChipType::Ground ||
                            chip == GameComponents::MapChipType::Grass ||
                            chip == GameComponents::MapChipType::Water;
                        if (!isOpenCell) {
                            continue;
                        }
                        // 駅前には RailPath に未接続でも常設レールが描かれる。
                        if (mapGenerator_->IsStationRailCell(cellX, cellZ) ||
                            railCells.contains(CellKey(auraX, auraZ))) {
                            continue;
                        }

                        auraCells.insert(CellKey(auraX, auraZ));
                    }
                }
            }

            DrawAuraCells(auraCells);
        }

        bool RunsWhileStopped() const override { return true; }

        void PostSceneFinalize(SceneContext&) override
        {
            mapGenerator_ = nullptr;
            railPath_ = nullptr;
            pauseMenu_ = nullptr;
            pauseMenuResolved_ = false;
            wavePhase_ = 0.0f;
        }

    private:
        void DrawAuraCells(const std::unordered_set<uint64_t>& cells) const
        {
            const float gridSize = std::max(0.01f, GameComponents::GameSettings::GridSize.Get());
            const float height = GameComponents::BlockModelLayout::GetSurfaceHeight(gridSize) +
                std::max(0.0f, cvHeightOffset.Get()) * gridSize;
            const float minSide = std::max(0.01f, cvMinSize.Get()) * gridSize;
            const float maxSide = std::max(minSide, cvMaxSize.Get() * gridSize);
            const int waveCount = std::clamp(cvWaveCount.Get(), 1, 8);
            const Vector4 color4 = cvColor.Get();
            const Vector3 color{ color4.x, color4.y, color4.z };
            const float maxAlpha = std::clamp(cvAlpha.Get() * color4.w, 0.0f, 1.0f);
            auto& lines = LineManager::GetInstance();

            for (const uint64_t key : cells) {
                const auto x = static_cast<int32_t>(key >> 32);
                const auto z = static_cast<int32_t>(key & 0xffffffffu);
                const float centerX = static_cast<float>(x) * gridSize;
                const float centerZ = static_cast<float>(z) * gridSize;

                for (int wave = 0; wave < waveCount; ++wave) {
                    const float offset = static_cast<float>(wave) /
                        static_cast<float>(waveCount);
                    const float progress = std::fmod(wavePhase_ + offset, 1.0f);
                    // 最初にすっと広がり、外周へ近づくほど減速する。
                    const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
                    const float sideLength = minSide + (maxSide - minSide) * eased;
                    const float alpha = maxAlpha * std::sin(progress * std::numbers::pi_v<float>);
                    DrawSquareWave(
                        lines, centerX, centerZ, height, sideLength, color, alpha);
                }
            }
        }

        GameComponents::MapGeneratorComponent* mapGenerator_ = nullptr;
        GameComponents::RailPathComponent* railPath_ = nullptr;
        GameComponents::PauseMenuUIComponent* pauseMenu_ = nullptr;
        bool pauseMenuResolved_ = false;
        float wavePhase_ = 0.0f;
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateBananaTreeAuraFeature()
{
    return std::make_unique<BananaTreeAuraFeature>();
}
