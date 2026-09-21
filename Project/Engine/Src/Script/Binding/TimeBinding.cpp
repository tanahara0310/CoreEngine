#include "pch.h"
#include "Script/Binding/TimeBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Utility/FrameRate/Time.h"

namespace CoreEngine::Script
{
    bool RegisterTimeBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar registrar(engine);
        registrar.Namespace("Time");
        registrar.Function("float DeltaTime()", asFUNCTION(Time::DeltaTime));
        registrar.Function("float UnscaledDeltaTime()", asFUNCTION(Time::UnscaledDeltaTime));
        registrar.Function("float TimeSinceStartup()", asFUNCTION(Time::TimeSinceStartup));
        registrar.Function("float UnscaledTimeSinceStartup()", asFUNCTION(Time::UnscaledTimeSinceStartup));
        registrar.Function("uint64 FrameCount()", asFUNCTION(Time::FrameCount));
        registrar.Function("float TimeScale()", asFUNCTION(Time::TimeScale));
        registrar.Function("bool IsPaused()", asFUNCTION(Time::IsPaused));
        // 速さはゲームが決めてよい（スローモー・ヒットストップ）。
        // 止めるのは SetTimeScale(0) で行う。Time::SetPaused は再生ボタンの持ち物なので出さない
        registrar.Function("void SetTimeScale(float scale)", asFUNCTION(Time::SetTimeScale));
        registrar.Namespace("");
        return registrar.Succeeded();
    }
}
