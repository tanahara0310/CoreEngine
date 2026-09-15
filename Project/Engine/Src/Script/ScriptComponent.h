#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Script/ScriptComponentType.h"

#include <string>

class asIScriptObject;

namespace CoreEngine
{
    class ScriptHost;

    /// @brief スクリプトのクラスのインスタンスを 1 つ持つコンポーネント
    /// @details 型名はスクリプトのクラス名。プロパティの保存・インスペクタ・Undo は型の記述子を通る。
    class ScriptComponent final : public IComponent
    {
    public:
        /// @brief 型のクラスのオブジェクトを作って持つ（作れなければ持たないまま）
        explicit ScriptComponent(const ScriptComponentType& type);
        ~ScriptComponent() override;

        const char* GetTypeName() const override { return typeName_.c_str(); }

        /// @return スクリプトのオブジェクトを持っていなければ nullptr
        const Reflection::TypeDescriptor* GetTypeDescriptor() const override;

        void* GetReflectionInstance() override { return this; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return displayName_.c_str(); }
#endif

        void Awake() override;
        void Start() override;
        void Update() override;
        void LateUpdate() override;
        void OnDestroy() override;

        /// @brief プロパティの値を読み出す（型の記述子の読み出しの口）
        /// @param out `property.type` に対応する型の実体
        void ReadProperty(const Reflection::PropertyDescriptor& property, void* out) const;

        /// @brief プロパティへ値を書き込む（型の記述子の書き込みの口）
        /// @param in `property.type` に対応する型の実体
        void WriteProperty(const Reflection::PropertyDescriptor& property, const void* in);

        /// @brief スクリプトのオブジェクトと型を手放す（実行環境を終える前に呼ばれる）
        void ReleaseScriptObject();

        /// @brief 持っているスクリプトのオブジェクト（無ければ nullptr）
        asIScriptObject* GetScriptObject() const { return object_; }

        /// @brief ライフサイクルの関数の呼び出しを表す名前（`オブジェクト名 の 型名::関数名`）
        std::string DescribeMethod(ScriptComponentType::Method method) const;

    private:
        /// @brief ライフサイクルの関数を呼ぶ（止まったらこのコンポーネントを無効にする）
        void Invoke(ScriptComponentType::Method method);

        std::string typeName_;
#ifdef USE_IMGUI
        std::string displayName_;
#endif
        const ScriptComponentType* type_ = nullptr;
        ScriptHost* host_ = nullptr;
        asIScriptObject* object_ = nullptr;
    };
}
