#include "pch.h"
#include "Script/Binding/RenderingBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ProjectPostEffect/ProjectPostEffect.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>

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

        /// @brief 画面全体の黒フェードの濃さを決める（0 = 透明 / 1 = 真っ黒）
        /// @details ほぼ透明ならパスごと切る（毎フレーム 1 枚ぶんの合成を省く）。
        void SetFadeAlpha(float alpha)
        {
            PostEffectManager* const postEffects =
                sEngineSystem ? sEngineSystem->GetService<PostEffectManager>() : nullptr;
            FadeEffect* const fade =
                postEffects ? postEffects->GetEffect<FadeEffect>(PostEffectNames::FadeEffect) : nullptr;
            if (!fade) {
                return;
            }
            const float clamped = (alpha < 0.0f) ? 0.0f : ((alpha > 1.0f) ? 1.0f : alpha);
            fade->SetFadeType(FadeEffect::FadeType::BlackFade);
            fade->SetEnabled(clamped > 0.001f);
            fade->SetFadeAlpha(clamped);
        }

        /// @brief スクリプトへ渡すマテリアル（.mat）の口。同じパスなら全員が同じ中身を共有する
        /// @details 値を変えると、そのマテリアルを指すスプライトと UI 画像の全部に効く。
        ///          再生を止めるとファイルの中身へ戻る。
        class ScriptShaderMaterial
        {
        public:
            explicit ScriptShaderMaterial(std::shared_ptr<ShaderMaterial> material) : material_(std::move(material)) {}

            ScriptShaderMaterial(const ScriptShaderMaterial&) = delete;
            ScriptShaderMaterial& operator=(const ScriptShaderMaterial&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            bool IsValid() const { return material_ != nullptr; }

            std::string GetPath() const { return material_ ? material_->GetAssetPath() : std::string(); }

            std::string GetShader() const { return material_ ? material_->GetShaderPath() : std::string(); }

            void SetFloat(const std::string& name, float value)
            {
                const float numbers[] = { value };
                Set(name, numbers);
            }

            void SetInt(const std::string& name, int value)
            {
                const float numbers[] = { static_cast<float>(value) };
                Set(name, numbers);
            }

            void SetVector(const std::string& name, const Vector4& value)
            {
                const float numbers[] = { value.x, value.y, value.z, value.w };
                Set(name, numbers);
            }

            void SetTexture(const std::string& name, const std::string& texturePath)
            {
                if (!material_) {
                    return;
                }
                MaterialTexture texture;
                texture.path = texturePath;
                if (const MaterialTexture* const current = material_->FindTexture(name)) {
                    texture.linear = current->linear;
                }
                material_->SetTexture(name, texture);
            }

            float GetFloat(const std::string& name) const
            {
                const MaterialNumbers* const numbers = material_ ? material_->FindNumbers(name) : nullptr;
                return (numbers && !numbers->empty()) ? numbers->front() : 0.0f;
            }

            Vector4 GetVector(const std::string& name) const
            {
                Vector4 value = { 0.0f, 0.0f, 0.0f, 0.0f };
                if (const MaterialNumbers* const numbers = material_ ? material_->FindNumbers(name) : nullptr) {
                    float* const components[] = { &value.x, &value.y, &value.z, &value.w };
                    for (size_t index = 0; index < numbers->size() && index < 4; ++index) {
                        *components[index] = (*numbers)[index];
                    }
                }
                return value;
            }

            void Revert()
            {
                if (material_) {
                    material_->Revert();
                }
            }

        private:
            ~ScriptShaderMaterial() = default;

            void Set(const std::string& name, std::span<const float> numbers)
            {
                if (material_) {
                    material_->SetNumbers(name, numbers);
                }
            }

            std::shared_ptr<ShaderMaterial> material_;
            mutable int refCount_ = 1;
        };

        /// @brief パスかファイル名でマテリアルを引く（`Application/Assets` からの相対パスでもよい）
        ScriptShaderMaterial* CreateShaderMaterial(const std::string& path)
        {
            std::string assetPath = path;
            const bool rooted = path.starts_with("Application/") || path.starts_with("Engine/");
            if (!rooted && path.find('/') != std::string::npos) {
                assetPath = "Application/Assets/" + path;
            }
            std::shared_ptr<ShaderMaterial> material = ShaderMaterialLibrary::GetInstance().Get(assetPath);
            if (!material) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "マテリアル {} が見つからないか読めません", path);
            }
            return new ScriptShaderMaterial(std::move(material));
        }

        /// @brief プロジェクトのポストエフェクトを名前で引く（無ければ名前ごとに 1 回だけ警告する）
        ProjectPostEffect* FindProjectEffectOrWarn(const std::string& name)
        {
            PostEffectManager* const postEffects =
                sEngineSystem ? sEngineSystem->GetService<PostEffectManager>() : nullptr;
            ProjectPostEffect* const effect = postEffects ? postEffects->FindProjectEffect(name) : nullptr;
            static std::unordered_set<std::string> warned;
            if (!effect && warned.insert(name).second) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "ポストエフェクト {} は Project.json の postEffects にありません（作れなかったときはエラーのログを見てください）", name);
            }
            return effect;
        }

        void PostEffectSetEnabled(const std::string& name, bool enabled)
        {
            if (FindProjectEffectOrWarn(name)) {
                sEngineSystem->GetService<PostEffectManager>()->SetEffectEnabled(name, enabled);
            }
        }

        bool PostEffectIsEnabled(const std::string& name)
        {
            const ProjectPostEffect* const effect = FindProjectEffectOrWarn(name);
            return effect && effect->IsEnabled();
        }

        ScriptShaderMaterial* PostEffectGetMaterial(const std::string& name)
        {
            const ProjectPostEffect* const effect = FindProjectEffectOrWarn(name);
            return new ScriptShaderMaterial(effect ? effect->GetMaterial() : nullptr);
        }

        void RegisterShaderMaterial(BindingRegistrar& r)
        {
            using Handle = ScriptShaderMaterial;
            r.ReferenceType("ShaderMaterial", asOBJ_REF);
            r.Behaviour("ShaderMaterial", asBEHAVE_FACTORY, "ShaderMaterial@ f(const string &in path)",
                asFUNCTION(CreateShaderMaterial), asCALL_CDECL);
            r.Behaviour("ShaderMaterial", asBEHAVE_ADDREF, "void f()", asMETHOD(Handle, AddRef), asCALL_THISCALL);
            r.Behaviour("ShaderMaterial", asBEHAVE_RELEASE, "void f()", asMETHOD(Handle, Release), asCALL_THISCALL);
            r.Method("ShaderMaterial", "bool get_isValid() const property", asMETHOD(Handle, IsValid), asCALL_THISCALL);
            r.Method("ShaderMaterial", "string get_path() const property", asMETHOD(Handle, GetPath), asCALL_THISCALL);
            r.Method("ShaderMaterial", "string get_shader() const property", asMETHOD(Handle, GetShader), asCALL_THISCALL);
            r.Method("ShaderMaterial", "void SetFloat(const string &in name, float value)", asMETHOD(Handle, SetFloat), asCALL_THISCALL);
            r.Method("ShaderMaterial", "void SetInt(const string &in name, int value)", asMETHOD(Handle, SetInt), asCALL_THISCALL);
            r.Method("ShaderMaterial", "void SetVector(const string &in name, const Vector4 &in value)",
                asMETHOD(Handle, SetVector), asCALL_THISCALL);
            r.Method("ShaderMaterial", "void SetTexture(const string &in name, const string &in texturePath)",
                asMETHOD(Handle, SetTexture), asCALL_THISCALL);
            r.Method("ShaderMaterial", "float GetFloat(const string &in name) const", asMETHOD(Handle, GetFloat), asCALL_THISCALL);
            r.Method("ShaderMaterial", "Vector4 GetVector(const string &in name) const", asMETHOD(Handle, GetVector), asCALL_THISCALL);
            r.Method("ShaderMaterial", "void Revert()", asMETHOD(Handle, Revert), asCALL_THISCALL);
        }
    }

    bool RegisterRenderingBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);
        r.Namespace("Rendering");
        r.Function("float GetAutoExposureEV()", asFUNCTION(GetAutoExposureEV));
        r.Function("void SetFadeAlpha(float alpha)", asFUNCTION(SetFadeAlpha));
        r.Namespace("");
        RegisterShaderMaterial(r);
        r.Namespace("PostEffect");
        r.Function("void SetEnabled(const string &in name, bool enabled)", asFUNCTION(PostEffectSetEnabled));
        r.Function("bool IsEnabled(const string &in name)", asFUNCTION(PostEffectIsEnabled));
        r.Function("ShaderMaterial@ GetMaterial(const string &in name)", asFUNCTION(PostEffectGetMaterial));
        r.Namespace("");
        return r.Succeeded();
    }
}
