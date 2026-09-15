#include "pch.h"
#include "Script/Binding/RenderingBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/ModelRenderPoolComponent.h"
#include "GameObject/Component/Render/TileWaterComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>

#include <cstdint>

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

        /// ログとスクリプトに出すコンポーネントの型名
        template <class Component> constexpr const char* kComponentTypeName = "";
        template <> constexpr const char* kComponentTypeName<MaterialComponent> = "Material";
        template <> constexpr const char* kComponentTypeName<ModelRenderPoolComponent> = "ModelRenderPool";
        template <> constexpr const char* kComponentTypeName<TileWaterComponent> = "TileWater";

        /// @brief スクリプトへ渡す描画のコンポーネント（Material / ModelRenderPool / TileWater）のハンドル
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびにコンポーネントを引き直す。
        ///          型ごとにある口は、その型を登録するときだけ使う。
        template <class Component>
        class ScriptRenderComponent
        {
        public:
            explicit ScriptRenderComponent(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }

            ScriptRenderComponent(const ScriptRenderComponent&) = delete;
            ScriptRenderComponent& operator=(const ScriptRenderComponent&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、コンポーネントが付いているか
            bool Exists() const { return Find() != nullptr; }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

            Vector4 GetColor() const
            {
                const Component* const component = FindOrWarn("色の読み取り");
                return component ? component->GetColor() : Vector4{};
            }

            void SetColor(const Vector4& color)
            {
                if (Component* const component = FindOrWarn("色の変更")) {
                    component->SetColor(color);
                }
            }

            bool Draw(const Vector3& position)
            {
                Component* const component = FindOrWarn("描画");
                return component ? component->Draw(position) : false;
            }

            bool DrawTransformed(const Vector3& position, const Vector3& rotation, const Vector3& scale)
            {
                Component* const component = FindOrWarn("描画");
                return component ? component->Draw(position, rotation, scale) : false;
            }

            bool DrawTinted(const Vector3& position, const Vector3& rotation, const Vector3& scale, const Vector4& color)
            {
                Component* const component = FindOrWarn("描画");
                return component ? component->Draw(position, rotation, scale, color) : false;
            }

            uint32_t GetCapacity() const
            {
                const Component* const component = FindOrWarn("容量の読み取り");
                return component ? static_cast<uint32_t>(component->GetCapacity()) : 0u;
            }

            uint32_t GetActiveCount() const
            {
                const Component* const component = FindOrWarn("使用数の読み取り");
                return component ? static_cast<uint32_t>(component->GetActiveCount()) : 0u;
            }

            void DrawSurface(float worldX, float worldZ)
            {
                if (Component* const component = FindOrWarn("水面の描画")) {
                    component->DrawSurface(worldX, worldZ);
                }
            }

            void DrawFall(float worldX, float edgeZ, bool facingNegativeZ)
            {
                if (Component* const component = FindOrWarn("滝の描画")) {
                    component->DrawFall(worldX, edgeZ, facingNegativeZ);
                }
            }

            float GetTileSize() const
            {
                const Component* const component = FindOrWarn("板の大きさの読み取り");
                return component ? component->GetTileSize() : 0.0f;
            }

            void SetTileSize(float size)
            {
                if (Component* const component = FindOrWarn("板の大きさの変更")) {
                    component->SetTileSize(size);
                }
            }

            float GetSurfaceHeight() const
            {
                const Component* const component = FindOrWarn("水面の高さの読み取り");
                return component ? component->GetSurfaceHeight() : 0.0f;
            }

            void SetSurfaceHeight(float height)
            {
                if (Component* const component = FindOrWarn("水面の高さの変更")) {
                    component->SetSurfaceHeight(height);
                }
            }

        private:
            ~ScriptRenderComponent() { owner_.Release(); }

            Component* Find() const
            {
                GameObject* const object = owner_.Resolve();
                return object ? object->GetComponent<Component>() : nullptr;
            }

            /// @brief コンポーネントを引き、引けなければ 1 回だけ警告する
            Component* FindOrWarn(const char* action) const
            {
                Component* const component = Find();
                if (!component && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "{} で{}をしようとしましたが、GameObject が無いか {} が付いていません",
                        kComponentTypeName<Component>, action, kComponentTypeName<Component>);
                }
                return component;
            }

            ScriptGameObject& owner_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptRenderComponent<MaterialComponent>* GetMaterial(ScriptGameObject& self)
        {
            return new ScriptRenderComponent<MaterialComponent>(self);
        }

        ScriptRenderComponent<ModelRenderPoolComponent>* GetModelRenderPool(ScriptGameObject& self)
        {
            return new ScriptRenderComponent<ModelRenderPoolComponent>(self);
        }

        ScriptRenderComponent<TileWaterComponent>* GetTileWater(ScriptGameObject& self)
        {
            return new ScriptRenderComponent<TileWaterComponent>(self);
        }

        /// @brief ハンドルの型と、参照の数え方・存在の確認を登録する
        template <class Component>
        void RegisterHandleType(BindingRegistrar& r)
        {
            using Handle = ScriptRenderComponent<Component>;
            const char* const name = kComponentTypeName<Component>;
            r.ReferenceType(name, asOBJ_REF);
            r.Behaviour(name, asBEHAVE_ADDREF, "void f()", asMETHOD(Handle, AddRef), asCALL_THISCALL);
            r.Behaviour(name, asBEHAVE_RELEASE, "void f()", asMETHOD(Handle, Release), asCALL_THISCALL);
            r.Method(name, "bool get_exists() const property", asMETHOD(Handle, Exists), asCALL_THISCALL);
        }
    }

    bool RegisterRenderingBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);

        using MaterialHandle = ScriptRenderComponent<MaterialComponent>;
        RegisterHandleType<MaterialComponent>(r);
        r.Method("Material", "Vector4 get_color() const property", asMETHOD(MaterialHandle, GetColor), asCALL_THISCALL);
        r.Method("Material", "void set_color(const Vector4 &in) property", asMETHOD(MaterialHandle, SetColor), asCALL_THISCALL);
        r.Method("Material", "GameObject@ get_gameObject() const property", asMETHOD(MaterialHandle, GetGameObject), asCALL_THISCALL);
        r.Method("GameObject", "Material@ get_material() property", asFUNCTION(GetMaterial), asCALL_CDECL_OBJLAST);

        using PoolHandle = ScriptRenderComponent<ModelRenderPoolComponent>;
        RegisterHandleType<ModelRenderPoolComponent>(r);
        r.Method("ModelRenderPool", "bool Draw(const Vector3 &in position)", asMETHOD(PoolHandle, Draw), asCALL_THISCALL);
        r.Method("ModelRenderPool", "bool Draw(const Vector3 &in position, const Vector3 &in rotation, const Vector3 &in scale)",
            asMETHOD(PoolHandle, DrawTransformed), asCALL_THISCALL);
        r.Method("ModelRenderPool", "bool Draw(const Vector3 &in position, const Vector3 &in rotation, const Vector3 &in scale, const Vector4 &in color)",
            asMETHOD(PoolHandle, DrawTinted), asCALL_THISCALL);
        r.Method("ModelRenderPool", "uint get_capacity() const property", asMETHOD(PoolHandle, GetCapacity), asCALL_THISCALL);
        r.Method("ModelRenderPool", "uint get_activeCount() const property", asMETHOD(PoolHandle, GetActiveCount), asCALL_THISCALL);
        r.Method("ModelRenderPool", "GameObject@ get_gameObject() const property", asMETHOD(PoolHandle, GetGameObject), asCALL_THISCALL);
        r.Method("GameObject", "ModelRenderPool@ get_modelRenderPool() property", asFUNCTION(GetModelRenderPool), asCALL_CDECL_OBJLAST);

        using WaterHandle = ScriptRenderComponent<TileWaterComponent>;
        RegisterHandleType<TileWaterComponent>(r);
        r.Method("TileWater", "void DrawSurface(float worldX, float worldZ)", asMETHOD(WaterHandle, DrawSurface), asCALL_THISCALL);
        r.Method("TileWater", "void DrawFall(float worldX, float edgeZ, bool facingNegativeZ)", asMETHOD(WaterHandle, DrawFall), asCALL_THISCALL);
        r.Method("TileWater", "float get_tileSize() const property", asMETHOD(WaterHandle, GetTileSize), asCALL_THISCALL);
        r.Method("TileWater", "void set_tileSize(float) property", asMETHOD(WaterHandle, SetTileSize), asCALL_THISCALL);
        r.Method("TileWater", "float get_surfaceHeight() const property", asMETHOD(WaterHandle, GetSurfaceHeight), asCALL_THISCALL);
        r.Method("TileWater", "void set_surfaceHeight(float) property", asMETHOD(WaterHandle, SetSurfaceHeight), asCALL_THISCALL);
        r.Method("TileWater", "GameObject@ get_gameObject() const property", asMETHOD(WaterHandle, GetGameObject), asCALL_THISCALL);
        r.Method("GameObject", "TileWater@ get_tileWater() property", asFUNCTION(GetTileWater), asCALL_CDECL_OBJLAST);

        r.Namespace("Rendering");
        r.Function("float GetAutoExposureEV()", asFUNCTION(GetAutoExposureEV));
        r.Namespace("");
        return r.Succeeded();
    }
}
