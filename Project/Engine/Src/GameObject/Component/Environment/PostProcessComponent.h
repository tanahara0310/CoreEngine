#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine
{
/// @brief ポストエフェクトをシーンに置くコンポーネント
/// @details パラメータの実体は CVar（`r.Bloom.*` など 22 種）が持ち、
///          保存はシーンの `_environment.json`。このコンポーネントは
///          「このシーンの色作り」がどこにあるかを表す置き場所。
/// @note チェックを外しても効果の列はそのまま通る（列ごと止める仕組みが
///       `PostEffectManager` に無い）。止めるときは効果ごとのトグルを使う。
class PostProcessComponent final : public IComponent {
public:
    const char* GetTypeName() const override { return "PostProcess"; }
};
}
