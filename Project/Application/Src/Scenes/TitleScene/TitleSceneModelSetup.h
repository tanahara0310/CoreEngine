#pragma once

#include <functional>
#include <string>

namespace CoreEngine
{
    class GameObject;
}

namespace TitleSceneModel
{
    /// @brief タイトルロゴ用 GameObject を生成する関数型
    /// @details BaseScene::CreateObject() は protected なので、シーン側から生成関数を
    ///          注入してもらう。これにより、このヘルパーは BaseScene の内部実装へ
    ///          直接依存せず、モデルの構成だけを担当できる。
    using ObjectFactory = std::function<CoreEngine::GameObject*(const std::string& name)>;

    /// @brief title.obj とそのアニメーションコンポーネントを構築する
    /// @param createObject シーンの GameObject 生成関数
    /// @param createIntroCompletionCallback イントロ完了通知を登録する関数
    /// @note title.json が存在しない場合でも、この関数から同じ構成を再現できる。
    using IntroCompletionCallbackFactory = std::function<std::function<void()>()>;

    void Build(
        const ObjectFactory& createObject,
        const IntroCompletionCallbackFactory& createIntroCompletionCallback = {});
}
