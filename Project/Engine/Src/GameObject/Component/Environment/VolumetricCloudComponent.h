#pragma once

#include "GameObject/Component/Environment/CVarToggleComponent.h"

namespace CoreEngine
{
/// @brief ボリュメトリック雲をシーンに置くコンポーネント
/// @details パラメータと有効・無効の実体は CVar（`r.Cloud.*`）が持ち、保存はシーンの `_environment.json`。
///          このコンポーネントは「シーンのどこに雲があるか」を表す置き場所。
///          チェックを切り替えると `r.Cloud.Enabled` へ写り、CVar 側で切り替えると
///          `EnvironmentFeature` がチェックへ写す。
class VolumetricCloudComponent final : public CVarToggleComponent {
public:
    const char* GetTypeName() const override { return "VolumetricCloud"; }
};
}
