#pragma once

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
    }
}
