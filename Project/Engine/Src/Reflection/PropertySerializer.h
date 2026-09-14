#pragma once

#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

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

        /// @brief AssetRef の値を保存形にする
        /// @return 何も指していなければ null。指していれば引いた先の今の GUID とパス（引けなければ持っている値）
        json AssetRefToJson(const AssetRefValue& value);

        /// @brief 保存形から AssetRef の値を読む
        /// @return null なら何も指さない値にして true。GUID もパスも読めなければ false（`out` は変えない）
        bool JsonToAssetRef(const json& node, AssetRefValue& out);
    }
}
