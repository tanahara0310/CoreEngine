#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Script/ScriptComponentType.h"

#include <cstdint>
#include <string>
#include <unordered_map>

class asIScriptObject;

namespace CoreEngine::Script
{
    class ScriptGameObject;
}

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

        /// @brief スクリプトのオブジェクトの `owner_` へ持ち主のハンドルを入れる
        void BindOwnerHandle();

        /// @brief 持ち主のハンドルからこのコンポーネントを外して手放す
        void ReleaseOwnerHandle();

        /// @brief スクリプトのパスの文字列と控えた GUID から、AssetRef の値を作る
        void ReadAssetRef(const Reflection::PropertyDescriptor& property, const std::string& path,
            Reflection::AssetRefValue& out) const;

        /// @brief AssetRef の値からパスを決めてスクリプトの文字列へ書き、GUID を控える
        void WriteAssetRef(const Reflection::PropertyDescriptor& property, const Reflection::AssetRefValue& in,
            std::string& path);

        /// @brief シーンの参照が変わった ObjectRef だけ、繋ぎ先を引き直してスクリプトのハンドルへ入れる
        void ApplyObjectRefs();

        /// @brief ObjectRef のプロパティ 1 つの繋ぎ先を引いて、スクリプトのハンドルへ入れる（見つからなければ null を入れる）
        void ApplyObjectRef(std::uint32_t index);

        /// @brief スクリプトのハンドルのメンバ変数を差し替える（前のハンドルを手放し、渡したハンドルの参照を 1 つ引き取る）
        void StoreHandle(std::uint32_t index, void* handle);

        std::string typeName_;
#ifdef USE_IMGUI
        std::string displayName_;
#endif
        const ScriptComponentType* type_ = nullptr;
        ScriptHost* host_ = nullptr;
        asIScriptObject* object_ = nullptr;

        /// 持ち主のハンドル（参照を 1 つ持つ）
        Script::ScriptGameObject* ownerHandle_ = nullptr;

        /// AssetRef のプロパティごとの、最後に書き込んだ GUID とパス（キーはメンバ変数の番号）。
        /// スクリプトのメンバ変数はパスの文字列だけを持つので、GUID はここに残す
        std::unordered_map<std::uint32_t, Reflection::AssetRefValue> assetRefs_;

        /// ObjectRef のプロパティ 1 つの繋ぎ先
        struct ObjectRefSlot
        {
            const Reflection::PropertyDescriptor* property = nullptr;
            Reflection::ObjectRefValue value;
            /// ハンドルへ入れたときの GameObjectManager の参照の番号（0 はまだ入れていない）
            std::uint64_t appliedEpoch = 0;
        };

        /// ObjectRef のプロパティごとの繋ぎ先（キーはメンバ変数の番号）。
        /// スクリプトのメンバ変数はハンドルだけを持つので、保存する ID と型名はここに残す
        std::unordered_map<std::uint32_t, ObjectRefSlot> objectRefs_;
    };
}
