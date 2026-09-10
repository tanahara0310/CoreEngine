#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Utility/CVar/CVar.h"

namespace GameComponents
{
    /// @brief GameScene の生成時とゲームプレイ中に参照する調整値。
    /// @details CVarSettingsSection により CVars.json へ自動保存される。
    namespace GameSettings
    {
        extern CoreEngine::CVar<float> BgmVolume;

        extern CoreEngine::CVar<float> GridSize;
        extern CoreEngine::CVar<int> MapSizeZ;
        extern CoreEngine::CVar<int> InitialMapSizeX;
        extern CoreEngine::CVar<int> RenderDistance;
        extern CoreEngine::CVar<int> CsvChunkSizeX;

        extern CoreEngine::CVar<int> BuilderStartX;
        extern CoreEngine::CVar<int> BuilderStartZ;
        extern CoreEngine::CVar<float> TrainMoveSpeed;

        extern CoreEngine::CVar<float> InitialStamina;
        extern CoreEngine::CVar<float> MaximumStamina;
        extern CoreEngine::CVar<bool> GameOverAtZeroStamina;
        extern CoreEngine::CVar<float> BananaRecovery;
        extern CoreEngine::CVar<float> BananaRecoveryMonkeyCorrectionRate;
        extern CoreEngine::CVar<float> AdditionalMonkeyCostRate;
        extern CoreEngine::CVar<float> RailStaminaCost;
        extern CoreEngine::CVar<float> RockStaminaCost;
        extern CoreEngine::CVar<float> BridgeStaminaCost;

        extern CoreEngine::CVar<int> GroundPoolCapacity;
        extern CoreEngine::CVar<int> WaterPoolCapacity;
        extern CoreEngine::CVar<int> StationPoolCapacity;
        extern CoreEngine::CVar<int> RockPoolCapacity;
        extern CoreEngine::CVar<int> HardRockPoolCapacity;
        extern CoreEngine::CVar<int> BananaTreePoolCapacity;
        extern CoreEngine::CVar<int> GrassPoolCapacity;
        extern CoreEngine::CVar<int> BridgePoolCapacity;
        extern CoreEngine::CVar<int> RailPoolCapacity;
        extern CoreEngine::CVar<int> RailLeftPoolCapacity;
        extern CoreEngine::CVar<int> RailRightPoolCapacity;


        extern CoreEngine::CVar<CoreEngine::Vector2> HudPosition;
        extern CoreEngine::CVar<float> HudFontSize;
        extern CoreEngine::CVar<CoreEngine::Vector4> HudColor;
        extern CoreEngine::CVar<CoreEngine::Vector4> HudOutlineColor;
        extern CoreEngine::CVar<float> HudOutlineWidth;
        extern CoreEngine::CVar<int> HudSortOrder;
    }

    /// @brief GameScene の調整値をオブジェクトインスペクターへ表示する。
    class GameSettingsComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "GameSettings"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "ゲーム設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }
        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.35f;
            outRgba[1] = 0.85f;
            outRgba[2] = 0.45f;
            outRgba[3] = 1.0f;
        }
        bool DrawInspector() override;
#endif
    };
}
