#pragma once

#include "GameObject/Component/Environment/CVarToggleComponent.h"

namespace CoreEngine
{
/// @brief 高さフォグをシーンに置くコンポーネント
/// @details パラメータと有効・無効の実体は CVar（`r.Fog.*`）が持ち、保存はシーンの `_environment.json`。
///          このコンポーネントは「シーンのどこに霧があるか」を表す置き場所。
///          チェックを切り替えると `r.Fog.Enabled` へ写り、CVar 側で切り替えると
///          `EnvironmentFeature` がチェックへ写す。
class HeightFogComponent final : public CVarToggleComponent {
public:
    const char* GetTypeName() const override { return "HeightFog"; }
};
}
