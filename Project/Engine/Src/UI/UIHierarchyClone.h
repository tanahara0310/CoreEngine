#pragma once

#include "GameObject/ObjectId.h"

#include <unordered_map>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;

    /// @brief UI の根と、その子孫（UI トランスフォームの子）をまとめて複製する
    /// @details 複製の中で互いを指す参照（親・繋いだ部品）は複製どうしへ付け替え、外を指す参照はそのまま残す。
    ///          根の複製は元と同じ親の子になる。複製はシーンに保存しない。
    /// @param manager 複製を置くシーンのオブジェクト
    /// @param root 複製する根（UI トランスフォームが要る）
    /// @param outCopies 元の ID から複製への対応（要らなければ nullptr）
    /// @return 根の複製（根に UI トランスフォームが無ければ nullptr）
    GameObject* CloneUIHierarchy(GameObjectManager& manager, const GameObject& root,
                                 std::unordered_map<ObjectId, GameObject*>* outCopies = nullptr);
}
