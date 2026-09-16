#include "pch.h"
#include "Script/ScriptCallback.h"

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/ScriptHost.h"

#include <angelscript.h>

#include <utility>

namespace CoreEngine::Script
{
    ScriptCallback::ScriptCallback(asIScriptFunction* function, std::string label)
        : function_(function)
        , label_(std::move(label))
    {
        if (!function_) {
            return;
        }
        if (ScriptHost* const host = ScriptHost::FromEngine(function_->GetEngine())) {
            hostLifetime_ = host->GetLifetimeToken();
            moduleGeneration_ = host->GetModuleGeneration();
        }
    }

    ScriptCallback::~ScriptCallback()
    {
        if (function_ && !hostLifetime_.expired()) {
            function_->Release();
        }
    }

    bool ScriptCallback::Invoke(const std::function<int(asIScriptContext*)>& setArguments)
    {
        if (!function_ || failed_) {
            return false;
        }
        const std::shared_ptr<void> lifetime = hostLifetime_.lock();
        if (!lifetime) {
            return false;
        }
        ScriptHost* const host = ScriptHost::FromEngine(function_->GetEngine());
        if (!host) {
            return false;
        }
        if (host->GetModuleGeneration() != moduleGeneration_) {
            // スクリプトを読み直した後なので、前のモジュールの関数は呼ばずに手放す
            function_->Release();
            function_ = nullptr;
            return false;
        }
        const bool finished = host->CallFunction(function_, setArguments, [this]() { return label_; });
        if (!finished) {
            failed_ = true;
        }
        return finished;
    }

    namespace
    {
        /// @brief 値型を `&in` で受け取るスクリプトの関数を包む
        template <typename Value>
        std::function<void(const Value&)> MakeValueAction(asIScriptFunction* function, std::string label)
        {
            if (!function) {
                return {};
            }
            auto callback = std::make_shared<ScriptCallback>(function, std::move(label));
            return [callback](const Value& value) {
                Value argument = value;
                callback->Invoke([&argument](asIScriptContext* context) { return context->SetArgObject(0, &argument); });
            };
        }
    }

    std::function<void()> MakeScriptAction(asIScriptFunction* function, std::string label)
    {
        if (!function) {
            return {};
        }
        auto callback = std::make_shared<ScriptCallback>(function, std::move(label));
        return [callback]() { callback->Invoke(); };
    }

    std::function<void(float)> MakeScriptFloatAction(asIScriptFunction* function, std::string label)
    {
        if (!function) {
            return {};
        }
        auto callback = std::make_shared<ScriptCallback>(function, std::move(label));
        return [callback](float value) {
            callback->Invoke([value](asIScriptContext* context) { return context->SetArgFloat(0, value); });
        };
    }

    std::function<void(const Vector2&)> MakeScriptVector2Action(asIScriptFunction* function, std::string label)
    {
        return MakeValueAction<Vector2>(function, std::move(label));
    }

    std::function<void(const Vector3&)> MakeScriptVector3Action(asIScriptFunction* function, std::string label)
    {
        return MakeValueAction<Vector3>(function, std::move(label));
    }

    std::function<void(const Vector4&)> MakeScriptVector4Action(asIScriptFunction* function, std::string label)
    {
        return MakeValueAction<Vector4>(function, std::move(label));
    }
}
