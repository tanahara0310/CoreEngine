#pragma once

#include "Animation/AnimationEvent.h"
#include "Script/Binding/ScriptRefCounted.h"

#include <string>

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief スクリプトへ渡すアニメーションのイベント（出たときの値の写し）
    class ScriptAnimationEvent : public ScriptRefCounted<ScriptAnimationEvent>
    {
    public:
        /// @return 参照を 1 つ持ったオブジェクト
        static ScriptAnimationEvent* Create(const AnimationEvent& event);

        std::string GetName() const { return event_.name; }
        std::string GetClip() const { return event_.clip; }
        std::string GetState() const { return event_.state; }
        std::string GetLayer() const { return event_.layer; }
        float GetFloat() const { return event_.floatValue; }
        int GetInt() const { return event_.intValue; }
        std::string GetString() const { return event_.stringValue; }

    private:
        friend class ScriptRefCounted<ScriptAnimationEvent>;
        ScriptAnimationEvent() = default;
        ~ScriptAnimationEvent() = default;

        AnimationEvent event_;
    };

    /// @brief アニメーションのイベントの関数に渡す `AnimationEvent` をスクリプトへ登録する
    /// @return すべて登録できたら true
    bool RegisterAnimationBinding(asIScriptEngine* engine);
}
