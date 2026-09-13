#pragma once

#include "Reflection/PropertyDescriptor.h"

#include <vector>

namespace CoreEngine::Reflection
{
    /// @brief 1 つの型のプロパティ一覧
    struct TypeDescriptor
    {
        const char* name = "";
        const char* displayName = "";
        uint32_t    version = 1;
        std::vector<PropertyDescriptor> properties;

        /// @brief プロパティの一部だけを持つか
        /// @details true なら、残りの保存と表示は `OnSerialize` / `OnDeserialize` / `DrawInspector` が受け持つ。
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
        void Register(const TypeDescriptor* descriptor);

        const TypeDescriptor* Find(const char* typeName) const;
        const std::vector<const TypeDescriptor*>& GetAll() const noexcept { return types_; }

        /// @brief 登録時に溜めた警告をログへ出す
        void FlushPendingWarnings();

    private:
        TypeRegistry() = default;
        ~TypeRegistry() = default;
        TypeRegistry(const TypeRegistry&) = delete;
        TypeRegistry& operator=(const TypeRegistry&) = delete;

        std::vector<const TypeDescriptor*> types_;
        std::vector<std::string> pendingWarnings_;
    };
}
