#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine
{
/// @brief 高さフォグをシーンに置くコンポーネント
/// @details パラメータの実体は CVar（`r.Fog.*`）が持ち、保存はシーンの `_environment.json`。
///          このコンポーネントは「シーンのどこに霧があるか」を表す置き場所で、
///          有効・無効だけを `r.Fog.Enabled` と行き来させる。
/// @note 行き来させるのは `EnvironmentFeature`（停止中も回る）。
///       コンポーネントの `Update()` は再生中しか呼ばれないのでここには書けない。
class HeightFogComponent final : public IComponent {
public:
    const char* GetTypeName() const override { return "HeightFog"; }
};
}
