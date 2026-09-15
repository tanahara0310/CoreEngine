#pragma once

#include <angelscript.h>

#include <string>

namespace CoreEngine::Script
{
    /// @brief スクリプトへの型と関数の登録をまとめて行い、失敗した宣言をログへ出す
    class BindingRegistrar
    {
    public:
        explicit BindingRegistrar(asIScriptEngine* engine) : engine_(engine) {}

        /// @brief 以降の登録先の名前空間を切り替える（空文字で名前空間の外）
        void Namespace(const char* name);

        void ValueType(const char* name, int byteSize, asQWORD flags);
        void ReferenceType(const char* name, asQWORD flags);
        void Behaviour(const char* type, asEBehaviours behaviour, const char* declaration,
                       const asSFuncPtr& function, asDWORD callConvention);
        void Method(const char* type, const char* declaration, const asSFuncPtr& function, asDWORD callConvention);
        void Property(const char* type, const char* declaration, int byteOffset);
        void Function(const char* declaration, const asSFuncPtr& function, asDWORD callConvention = asCALL_CDECL);
        void Funcdef(const char* declaration);
        void Enum(const char* name);
        void EnumValue(const char* type, const char* name, int value);

        /// @brief ここまでの登録がすべて成功したか
        bool Succeeded() const { return succeeded_; }

    private:
        void Check(int result, const char* what, const std::string& declaration);

        asIScriptEngine* engine_ = nullptr;
        bool succeeded_ = true;
    };
}
