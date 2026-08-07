#pragma once

#ifdef USE_IMGUI

namespace CoreEngine
{
class GameObject;

/// @brief Inspector のコライダー編集セクション
/// @details GameObject 派生ならどのクラスからでも呼べるように独立させてある。
///          形状・サイズ・オフセット・レイヤー・トリガー/静止フラグを編集できる。
namespace ColliderInspector
{
    /// @brief コライダー一覧と編集 UI を描画する
    /// @param object 対象オブジェクト
    /// @return 値が変更されたら true
    bool Draw(GameObject& object);
}
}

#endif // USE_IMGUI
