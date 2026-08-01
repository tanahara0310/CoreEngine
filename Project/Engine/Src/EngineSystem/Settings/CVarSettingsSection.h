#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief CVar の自動保存セクション

namespace CoreEngine
{
    /// @brief 登録済み CVar をまとめて 1 ファイルへ自動保存するセクション
    /// @details 機能ごとに IEditorSettingsSection を実装する必要をなくすのが目的。
    ///          CVar を 1 つ定義すれば、ここへ何も追記しなくても保存・復元される。
    ///          保存先: Application/Saved/EditorSettings/CVars.json
    ///
    ///          キーは CVar のフルネーム（"r.Vignette.Intensity"）をそのまま使うフラット形式。
    ///          CVarFlags::NoSave が付いた変数は対象外。
    ///
    /// @note このセクションが登録される時点で存在する CVar だけが復元対象になる。
    ///       CVar は静的初期化で構築されるため通常は全て揃っているが、関数内 static や
    ///       動的生成した CVar は復元されないので、必ずファイルスコープで定義すること。
    class CVarSettingsSection : public IEditorSettingsSection
    {
    public:
        const char* GetSectionName() const override { return "CVars"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
    };
}
