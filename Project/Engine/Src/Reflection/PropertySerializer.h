#pragma once

#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <cstdint>

namespace CoreEngine::Reflection
{
    struct TypeDescriptor;

    /// @brief TypeDescriptor をたどって JSON と値を往復させる
    namespace PropertySerializer
    {
        /// @brief 保存対象のプロパティを JSON へ書き出す
        void Save(const TypeDescriptor& type, const void* instance, json& out);

        /// @brief JSON から値を復元する（キーが無いプロパティは現在値を維持）
        void Load(const TypeDescriptor& type, void* instance, const json& in);

        /// @brief 1 つのプロパティの値を保存形にする
        json PropertyToJson(const PropertyDescriptor& property, const void* instance);

        /// @brief 保存形の値を 1 つのプロパティへ書き込む
        /// @return 読み出して書き戻せたら true（形が合わない値のときは今の値のまま書き戻す）
        bool JsonToProperty(const PropertyDescriptor& property, void* instance, const json& node);

        /// @brief AssetRef の値を保存形にする
        /// @return 何も指していなければ null。指していれば引いた先の今の GUID とパス（引けなければ持っている値）
        json AssetRefToJson(const AssetRefValue& value);

        /// @brief 保存形から AssetRef の値を読む
        /// @return null なら何も指さない値にして true。GUID もパスも読めなければ false（`out` は変えない）
        bool JsonToAssetRef(const json& node, AssetRefValue& out);

        /// @brief 古い版で保存した `parameters` を、型の今の版の形へ書き換える
        /// @param savedVersion 保存したときの版
        /// @return 保存した版が型の版より新しければ false（`parameters` は変えない）
        /// @note 1 版ずつ、その版の改名（`TypeDescriptor::renames`）を済ませてから `TypeDescriptor::upgrade` を呼ぶ。
        bool MigrateParameters(const TypeDescriptor& type, uint32_t savedVersion, json& parameters);

        /// @brief 保存した版の数値を読む
        /// @return 1 以上の整数でなければ 1
        uint32_t ReadVersion(const json& node);

        /// @brief コンポーネントの保存形 `{"type", "enabled", "version", "parameters"}` から版を読む
        /// @return `version` が無ければ 1
        uint32_t ReadComponentVersion(const json& entry);

        /// @brief コンポーネントの保存形へ版を書く（1 以下なら `version` を消す）
        void WriteComponentVersion(json& entry, uint32_t version);

        /// @brief コンポーネントの保存形の `parameters` を型の今の版の形へ書き換え、`version` を今の版にする
        /// @return 保存した版が型の版より新しければ false（何も変えない）
        bool UpgradeComponentEntry(const TypeDescriptor& type, json& entry);
    }
}
