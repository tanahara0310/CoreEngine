#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine
{
/// @brief ボリュメトリック雲をシーンに置くコンポーネント
/// @details パラメータの実体は CVar（`r.Cloud.*`）が持ち、保存はシーンの `_environment.json`。
///          このコンポーネントは「シーンのどこに雲があるか」を表す置き場所で、
///          有効・無効だけを `r.Cloud.Enabled` と行き来させる。
/// @note 行き来させるのは `EnvironmentFeature`（停止中も回る）。
///       コンポーネントの `Update()` は再生中しか呼ばれないのでここには書けない。
class VolumetricCloudComponent final : public IComponent {
public:
    const char* GetTypeName() const override { return "VolumetricCloud"; }
};
}
