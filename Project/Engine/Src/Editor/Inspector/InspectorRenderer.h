#pragma once

#ifdef USE_IMGUI

#include <functional>

namespace CoreEngine::Reflection
{
    struct TypeDescriptor;
    struct PropertyDescriptor;
}

namespace CoreEngine
{
    /// @brief TypeDescriptor からインスペクタの中身を組み立てる
    namespace InspectorRenderer
    {
        /// @brief 編集が確定したときの通知（ドラッグを離した 1 回だけ）
        /// @param 第 2 引数は編集前の値へのポインタ
        using EditCommitted =
            std::function<void(const Reflection::PropertyDescriptor&, const void*)>;

        /// @brief 全プロパティの編集 UI を描く
        /// @param onCommitted 編集確定の通知先（省略可）。Undo はここから積む
        /// @return 値が変更されたら true
        bool Draw(const Reflection::TypeDescriptor& type, void* instance,
                  const EditCommitted& onCommitted = {});

        /// @brief 新経路を使うか（CVar d.Editor.UseReflectionInspector）
        bool IsEnabled();
    }
}

#endif // USE_IMGUI
