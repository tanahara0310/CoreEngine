#include "pch.h"
#include "Script/Binding/RenderingBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
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
        return r.Succeeded();
    }
}
