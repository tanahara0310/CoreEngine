#include "pch.h"
#include "CVarSettingsSection.h"
#include "Utility/CVar/CVarSerialization.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

namespace CoreEngine
{
    void CVarSettingsSection::Serialize(nlohmann::json& out) const
    {
        // コードデフォルトのままの項目は書かない。
        // 全件書くと、後からコード側の既定値を変更しても保存済みの古い値が勝ってしまう。
        // 触った項目だけが並ぶのでファイルが「自分の変更点一覧」にもなる
        CVarSerialization::Save(out, "", /*skipDefaults=*/true);
    }

    void CVarSettingsSection::Deserialize(const nlohmann::json& in)
    {
        CVarSerialization::Load(in);
    }
}
