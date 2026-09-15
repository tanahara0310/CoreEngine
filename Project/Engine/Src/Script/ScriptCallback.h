#pragma once

#include <functional>
#include <memory>
#include <string>

class asIScriptContext;
class asIScriptFunction;

namespace CoreEngine
{
    struct Vector2;
    struct Vector3;
    struct Vector4;
}

namespace CoreEngine::Script
{
    /// @brief スクリプトの関数（デリゲートを含む）を 1 つ持ち、C++ から呼ぶ
    /// @details 関数の参照を 1 つ持ち、壊すときに手放す（実行環境が先に終わっていたら手放さない）。
    ///          1 回でも最後まで実行できなかったら、以後は呼ばない。
    class ScriptCallback
    {
    public:
        /// @param function 参照を 1 つ受け取って持つ関数（nullptr なら何もしない）
        /// @param label 止まったときのログに出す呼び出し元の名前
        ScriptCallback(asIScriptFunction* function, std::string label);
        ~ScriptCallback();

        ScriptCallback(const ScriptCallback&) = delete;
        ScriptCallback& operator=(const ScriptCallback&) = delete;

        /// @brief 関数を呼ぶ
        /// @param setArguments コンテキストへ引数を積む（負の値を返したら実行しない。nullptr なら引数なし）
        /// @return 最後まで実行できたら true
        bool Invoke(const std::function<int(asIScriptContext*)>& setArguments = nullptr);

    private:
        asIScriptFunction* function_ = nullptr;
        std::weak_ptr<void> hostLifetime_;
        std::string label_;
        bool failed_ = false;
    };

    /// @brief 引数なしのスクリプトの関数を std::function に包む
    /// @param function 参照を 1 つ受け取って持つ関数（nullptr なら空の std::function を返す）
    std::function<void()> MakeScriptAction(asIScriptFunction* function, std::string label);

    /// @brief float を 1 つ受け取るスクリプトの関数を std::function に包む
    std::function<void(float)> MakeScriptFloatAction(asIScriptFunction* function, std::string label);

    /// @brief `const Vector2 &in` を受け取るスクリプトの関数を std::function に包む
    std::function<void(const Vector2&)> MakeScriptVector2Action(asIScriptFunction* function, std::string label);

    /// @brief `const Vector3 &in` を受け取るスクリプトの関数を std::function に包む
    std::function<void(const Vector3&)> MakeScriptVector3Action(asIScriptFunction* function, std::string label);

    /// @brief `const Vector4 &in` を受け取るスクリプトの関数を std::function に包む
    std::function<void(const Vector4&)> MakeScriptVector4Action(asIScriptFunction* function, std::string label);
}
