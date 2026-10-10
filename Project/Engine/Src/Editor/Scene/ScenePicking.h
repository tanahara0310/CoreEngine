#pragma once

#ifdef CORE_EDITOR

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"

#include <optional>

namespace CoreEngine
{
    class Camera;
    class GameObject;
    class GameObjectManager;
}

namespace CoreEngine::Editor::ScenePicking
{
    /// @brief ワールドのレイ（direction は正規化済み）
    struct Ray
    {
        Vector3 origin{};
        Vector3 direction{ 0.0f, 0.0f, 1.0f };
    };

    /// @brief レイが当たったところ
    struct Hit
    {
        GameObject* object = nullptr;
        Vector3 point{};

        /// レイの来た側を向いた面の向き（球で判定したときは球の外向き）
        Vector3 normal{ 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
    };

    /// @brief メッシュを持たないオブジェクトの扱い
    enum class Fallback
    {
        /// 当てない
        None,

        /// スケールの最大成分（最低 1）を半径にした球で当てる
        Sphere,
    };

    /// @brief ビューの中の位置（左上 0・右下 1）からワールドのレイを作る
    Ray ScreenToRay(const Vector2& normalizedPosition, const Camera& camera);

    /// @brief オブジェクト 1 つとレイの交差（メッシュの三角形）
    /// @return 当たれば true
    bool RaycastObject(const Ray& ray, GameObject& object, Fallback fallback, Hit& hit);

    /// @brief シーンの有効なオブジェクトのうち、レイが最初に当たるもの
    /// @details 3D の位置を持たないもの（UI など）と、分離表示で隠れているものは除く。
    std::optional<Hit> Raycast(const GameObjectManager& objects, const Ray& ray, Fallback fallback);
}

#endif // CORE_EDITOR
