#pragma once

#include "Reflection/TypeDescriptor.h"

#include <array>
#include <cstddef>
#include <deque>
#include <string>

class asIScriptFunction;
class asITypeInfo;
class CScriptBuilder;

namespace CoreEngine
{
    class ScriptHost;

    namespace Script
    {
        struct MetadataAttribute;
    }

    /// @brief ScriptComponent を継いだスクリプトのクラス 1 つ分のコンポーネントの型
    /// @details クラスの公開メンバ変数とその属性から記述子を組み立て、ライフサイクルの関数を控える。
    class ScriptComponentType
    {
    public:
        /// @brief スクリプトのクラスに書けるライフサイクルの関数
        enum class Method : std::size_t
        {
            Awake,
            Start,
            Update,
            LateUpdate,
            OnDestroy,
            Count,
        };

        /// @param host このクラスをコンパイルした実行環境
        /// @param type 組み立てるクラス
        /// @param base スクリプトの ScriptComponent クラス
        /// @param builder このクラスをコンパイルした scriptbuilder（属性を読む）
        ScriptComponentType(ScriptHost& host, asITypeInfo* type, asITypeInfo* base, CScriptBuilder& builder);
        ~ScriptComponentType();

        ScriptComponentType(const ScriptComponentType&) = delete;
        ScriptComponentType& operator=(const ScriptComponentType&) = delete;

        /// @brief 型名（クラス名）
        const std::string& GetName() const { return name_; }

        /// @brief インスペクタでの名前（`[DisplayName]` が無ければ型名）
        const std::string& GetDisplayName() const { return displayName_; }

        ScriptHost& GetHost() const { return host_; }
        asITypeInfo* GetTypeInfo() const { return typeInfo_; }
        const Reflection::TypeDescriptor& GetDescriptor() const { return descriptor_; }

        /// @brief ライフサイクルの関数
        /// @return このクラスが書いていなければ nullptr（ScriptComponent の空の関数のままのとき）
        asIScriptFunction* GetMethod(Method method) const
        {
            return methods_[static_cast<std::size_t>(method)];
        }

        /// @brief ライフサイクルの関数の名前（ログ用）
        static const char* GetMethodName(Method method);

    private:
        void ReadClassAttributes(CScriptBuilder& builder);
        void BuildProperties(asITypeInfo* base, CScriptBuilder& builder);
        void FindMethods(asITypeInfo* base);

        /// @brief 属性 1 つをプロパティへ当てる
        /// @return 当てられなければ false（`error` に理由）
        bool ApplyPropertyAttribute(Reflection::PropertyDescriptor& property,
                                    const Script::MetadataAttribute& attribute, std::string& error);

        /// @brief 記述子が指す文字列を控えて、そのアドレスを返す
        const char* StoreText(std::string text);

        ScriptHost& host_;
        asITypeInfo* typeInfo_ = nullptr;
        std::string name_;
        std::string displayName_;
        Reflection::TypeDescriptor descriptor_;
        std::array<asIScriptFunction*, static_cast<std::size_t>(Method::Count)> methods_{};

        /// 記述子の文字列の置き場（足しても要素のアドレスが変わらない入れ物）
        std::deque<std::string> texts_;
    };
}
