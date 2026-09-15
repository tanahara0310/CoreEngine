#include "pch.h"
#include "Script/Binding/RenderingBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>

namespace CoreEngine::Script
{
    namespace
    {
        /// 描画のサービスを引く先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        /// @return トーンマップが無ければ 0
        float GetAutoExposureEV()
        {
            PostEffectManager* const postEffects = sEngineSystem ? sEngineSystem->GetService<PostEffectManager>() : nullptr;
            const ToneMapping* const toneMapping =
                postEffects ? postEffects->GetEffect<ToneMapping>(PostEffectNames::ToneMapping) : nullptr;
            return toneMapping ? toneMapping->GetAutoExposureEV() : 0.0f;
        }

        /// @brief スクリプトへ渡すマテリアルのハンドル
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびに MaterialComponent を引き直す。
        class ScriptMaterial
        {
        public:
            explicit ScriptMaterial(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }

            ScriptMaterial(const ScriptMaterial&) = delete;
            ScriptMaterial& operator=(const ScriptMaterial&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、Material が付いているか
            bool Exists() const { return Find() != nullptr; }

            Vector4 GetColor() const
            {
                const MaterialComponent* const material = FindOrWarn("色の読み取り");
                return material ? material->GetColor() : Vector4{};
            }

            void SetColor(const Vector4& color)
            {
                if (MaterialComponent* const material = FindOrWarn("色の変更")) {
                    material->SetColor(color);
                }
            }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

        private:
            ~ScriptMaterial() { owner_.Release(); }

            MaterialComponent* Find() const
            {
                GameObject* const object = owner_.Resolve();
                return object ? object->GetComponent<MaterialComponent>() : nullptr;
            }

            /// @brief MaterialComponent を引き、引けなければ 1 回だけ警告する
            MaterialComponent* FindOrWarn(const char* action) const
            {
                MaterialComponent* const material = Find();
                if (!material && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "Material で{}をしようとしましたが、GameObject が無いか Material が付いていません", action);
                }
                return material;
            }

            ScriptGameObject& owner_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptMaterial* GetMaterial(ScriptGameObject& self)
        {
            return new ScriptMaterial(self);
        }
    }

    bool RegisterRenderingBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);
        r.ReferenceType("Material", asOBJ_REF);
        r.Behaviour("Material", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptMaterial, AddRef), asCALL_THISCALL);
        r.Behaviour("Material", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptMaterial, Release), asCALL_THISCALL);
        r.Method("Material", "bool get_exists() const property", asMETHOD(ScriptMaterial, Exists), asCALL_THISCALL);
        r.Method("Material", "Vector4 get_color() const property", asMETHOD(ScriptMaterial, GetColor), asCALL_THISCALL);
        r.Method("Material", "void set_color(const Vector4 &in) property", asMETHOD(ScriptMaterial, SetColor), asCALL_THISCALL);
        r.Method("Material", "GameObject@ get_gameObject() const property", asMETHOD(ScriptMaterial, GetGameObject), asCALL_THISCALL);
        r.Method("GameObject", "Material@ get_material() property", asFUNCTION(GetMaterial), asCALL_CDECL_OBJLAST);

        r.Namespace("Rendering");
        r.Function("float GetAutoExposureEV()", asFUNCTION(GetAutoExposureEV));
        r.Namespace("");
        return r.Succeeded();
    }
}
