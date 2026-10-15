#include "pch.h"
#include "Script/Binding/CameraRigBinding.h"

#include "Camera/Rig/CameraRig.h"
#include "Script/Binding/BindingRegistrar.h"

#include <algorithm>
#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// @brief 名前でリグを動かし始め、今の構図から blendSeconds 秒かけて繋ぐ
        bool Activate(const std::string& name, float blendSeconds, bool unscaledTime)
        {
            CameraRigActivateOptions options;
            options.blendSeconds = (std::max)(blendSeconds, 0.0f);
            options.useUnscaledTime = unscaledTime;
            return CameraRig::Activate(name, options);
        }

        void Deactivate()
        {
            CameraRig::Deactivate();
        }

        bool IsActive()
        {
            return CameraRig::IsActive();
        }

        std::string GetActiveName()
        {
            return CameraRig::GetActiveName();
        }
    }

    bool RegisterCameraRigBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }

        BindingRegistrar r(engine);
        r.Namespace("CameraRig");
        r.Function("bool Activate(const string &in name, float blendSeconds = 0.0f, bool unscaledTime = false)",
            asFUNCTION(Activate));
        r.Function("void Deactivate()", asFUNCTION(Deactivate));
        r.Function("bool IsActive()", asFUNCTION(IsActive));
        r.Function("string GetActiveName()", asFUNCTION(GetActiveName));
        r.Namespace("");
        return r.Succeeded();
    }
}
