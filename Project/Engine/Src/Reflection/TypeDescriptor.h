#pragma once

#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine::Reflection
{
    /// @brief 保存キーの改名 1 件
    struct KeyRename
    {
        uint32_t    version = 0;  ///< 新しいキーで保存するようになった版
        const char* from = "";    ///< それより前の版での保存キー
        const char* to = "";      ///< その版からの保存キー
    };

    /// @brief 保存値を 1 版ぶん書き換える関数
    /// @param fromVersion 書き換える前の版
    /// @param toVersion 書き換えた後の版（`fromVersion + 1`）
    /// @param parameters コンポーネントの `parameters`。その版の改名は済んでいる。
    ///        プレハブの上書きから呼ぶときは、上書きしたキーだけを持つ
    using UpgradeFunction = void (*)(uint32_t fromVersion, uint32_t toVersion, json& parameters);

    /// @brief 1 つの型のプロパティ一覧
    struct TypeDescriptor
    {
        const char* name = "";
        const char* displayName = "";

        /// @brief 保存形の版
        uint32_t    version = 1;

        std::vector<PropertyDescriptor> properties;

        /// @brief 保存キーの移行表
        std::vector<KeyRename> renames;

        /// @brief キーの改名だけでは表せない書き換え（無ければ nullptr）
        UpgradeFunction upgrade = nullptr;

        /// @brief プロパティの一部だけを持つか
        /// @details true なら、残りの保存は `OnSerialize` / `OnDeserialize`、表示はエディタの型ごとの登録が受け持つ。
        bool partial = false;

        const PropertyDescriptor* Find(const char* propertyName) const;
    };

    /// @brief 型名から TypeDescriptor を引くレジストリ
    /// @details 静的初期化中に各型が自分を登録する。
    class TypeRegistry
    {
    public:
        static TypeRegistry& Get();

        /// @brief 型を登録する（同名が既にあれば無視して警告を溜める）
        /// @note 版と移行表が食い違う型（移行の無い版がある・改名の版が範囲外）はエラーを溜める。
        void Register(const TypeDescriptor* descriptor);

        const TypeDescriptor* Find(const char* typeName) const;
        const std::vector<const TypeDescriptor*>& GetAll() const noexcept { return types_; }

        /// @brief 登録時に溜めた警告とエラーをログへ出す
        void FlushPendingWarnings();

    private:
        TypeRegistry() = default;
        ~TypeRegistry() = default;
        TypeRegistry(const TypeRegistry&) = delete;
        TypeRegistry& operator=(const TypeRegistry&) = delete;

        std::vector<const TypeDescriptor*> types_;
        std::vector<std::string> pendingWarnings_;
        std::vector<std::string> pendingErrors_;
    };
}
