#include "pch.h"
#include "Script/Binding/RenderingBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Script/Binding/BindingRegistrar.h"

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
        r.Namespace("");
        return r.Succeeded();
    }
}
