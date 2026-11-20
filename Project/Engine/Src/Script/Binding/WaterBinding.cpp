#include "pch.h"
#include "Script/Binding/WaterBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Math/Vector/Vector3.h"
#include "Script/Binding/BindingRegistrar.h"

#include <angelscript.h>

namespace CoreEngine::Script
{
    namespace
    {
        /// 水の波紋を引く先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        void AddRipple(const Vector3& position, float radius, float depth)
        {
            if (WaterRippleSimulation* const ripple = FindWaterRippleSimulation(sEngineSystem)) {
                ripple->AddRipple(position, radius, depth);
            }
        }
    }

    bool RegisterWaterBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);
        r.Namespace("Water");
        r.Function("void AddRipple(const Vector3 &in position, float radius, float depth)", asFUNCTION(AddRipple));
        r.Namespace("");
        return r.Succeeded();
    }
}
