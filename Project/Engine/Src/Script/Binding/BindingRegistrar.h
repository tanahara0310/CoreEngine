#pragma once

#include <angelscript.h>

#include <new>
#include <string>

namespace CoreEngine::Script
{
    /// スクリプトから見たエンジンの名前空間（`using namespace CoreEngine;` で頭を省ける）
    inline constexpr const char* kEngineNamespace = "CoreEngine";

    /// @brief 値型を既定のコンストラクタで作る（asBEHAVE_CONSTRUCT・asCALL_CDECL_OBJLAST で登録する）
    template <class T>
    void ConstructDefault(T* self)
    {
        new (self) T();
    }

    /// @brief 値型を写しで作る（asBEHAVE_CONSTRUCT・asCALL_CDECL_OBJLAST で登録する）
    template <class T>
    void ConstructCopy(const T& other, T* self)
    {
        new (self) T(other);
    }

    /// @brief 値型を壊す（asBEHAVE_DESTRUCT・asCALL_CDECL_OBJLAST で登録する）
    template <class T>
    void DestructValue(T* self)
    {
        self->~T();
    }

    /// @brief 参照型の参照を 1 つ増やす（RefCountBehaviours が登録する）
    template <class T>
    void AddRefOf(T* self)
    {
        self->AddRef();
    }

    /// @brief 参照型の参照を 1 つ減らす（RefCountBehaviours が登録する）
    template <class T>
    void ReleaseOf(T* self)
    {
        self->Release();
    }

    /// @brief スクリプトへの型と関数の登録をまとめて行い、失敗した宣言をログへ出す
    class BindingRegistrar
    {
    public:
        explicit BindingRegistrar(asIScriptEngine* engine) : engine_(engine) {}

        /// @brief 以降の登録先の名前空間を CoreEngine の下で切り替える（空文字で CoreEngine そのもの）
        void Namespace(const char* name);

        void ValueType(const char* name, int byteSize, asQWORD flags);
        void ReferenceType(const char* name, asQWORD flags);
        void Behaviour(const char* type, asEBehaviours behaviour, const char* declaration,
                       const asSFuncPtr& function, asDWORD callConvention);
        void Method(const char* type, const char* declaration, const asSFuncPtr& function, asDWORD callConvention);

        /// @brief 参照型の AddRef と Release をこの順で登録する
        /// @tparam T `AddRef() const` と `Release() const` を持つ型（ScriptRefCounted を継ぐ型など）
        template <class T>
        void RefCountBehaviours(const char* type)
        {
            Behaviour(type, asBEHAVE_ADDREF, "void f()", asFUNCTION(AddRefOf<T>), asCALL_CDECL_OBJLAST);
            Behaviour(type, asBEHAVE_RELEASE, "void f()", asFUNCTION(ReleaseOf<T>), asCALL_CDECL_OBJLAST);
        }

        /// @brief 汎用の呼び出し規約でメソッドを登録する
        /// @param auxiliary 関数の中で `asIScriptGeneric::GetAuxiliary()` から読む値
        void GenericMethod(const char* type, const char* declaration, asGENFUNC_t function, void* auxiliary);
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
