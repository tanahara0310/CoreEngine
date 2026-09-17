#pragma once

#ifdef USE_IMGUI

#include <functional>

namespace CoreEngine
{
    class GameObject;
}

/// @brief オブジェクトのインスペクタ（見出し・プレハブ・コンポーネントのセクション・追加ボタン）
namespace CoreEngine::Editor::ObjectInspector
{
    /// @brief 呼び出し側が渡す処理
    struct Callbacks
    {
        /// @brief ⋮ の「このオブジェクトだけ保存」で呼ぶ（空ならメニューを選べない）
        std::function<void(GameObject&)> saveObject;
    };

    /// @brief オブジェクトのインスペクタを描く
    /// @return 値を変えたら true
    bool Draw(GameObject& object, const Callbacks& callbacks);
}

#endif // USE_IMGUI
