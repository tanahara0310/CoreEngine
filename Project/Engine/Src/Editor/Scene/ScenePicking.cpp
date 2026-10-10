#include "pch.h"
#include "Editor/Scene/ScenePicking.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelData.h"
#include "Graphics/Model/ModelResource.h"
#include "Math/Geometry/RayCast.h"
#include "Math/Geometry/Shapes.h"
#include "Math/MathCore.h"
#include "Math/Matrix/Matrix4x4.h"
#include "WorldTransform/WorldTransform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CoreEngine::Editor::ScenePicking
{
    namespace
    {
        /// @brief 方向ベクトルを行列で変換（平行移動を無視・非正規化のまま返す）
        Vector3 TransformDirection(const Vector3& direction, const Matrix4x4& matrix)
        {
            return Vector3(
                direction.x * matrix.m[0][0] + direction.y * matrix.m[1][0] + direction.z * matrix.m[2][0],
                direction.x * matrix.m[0][1] + direction.y * matrix.m[1][1] + direction.z * matrix.m[2][1],
                direction.x * matrix.m[0][2] + direction.y * matrix.m[1][2] + direction.z * matrix.m[2][2]);
        }

        /// @brief 点を行列で変換（w で割る）
        Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix)
        {
            const float x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
            const float y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
            const float z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
            const float w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
            return w != 0.0f ? Vector3(x / w, y / w, z / w) : Vector3(x, y, z);
        }

        /// @brief ローカルの面の向きをワールドへ（逆行列の転置で変換して正規化）
        Vector3 TransformNormal(const Vector3& normal, const Matrix4x4& inverseWorld)
        {
            const Vector3 world(
                normal.x * inverseWorld.m[0][0] + normal.y * inverseWorld.m[0][1] + normal.z * inverseWorld.m[0][2],
                normal.x * inverseWorld.m[1][0] + normal.y * inverseWorld.m[1][1] + normal.z * inverseWorld.m[1][2],
                normal.x * inverseWorld.m[2][0] + normal.y * inverseWorld.m[2][1] + normal.z * inverseWorld.m[2][2]);
            const float length = std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
            return length > 0.0f ? Vector3(world.x / length, world.y / length, world.z / length) : Vector3(0.0f, 1.0f, 0.0f);
        }

        /// @brief メッシュを持たないオブジェクトの代わりの球の半径（スケールの最大成分。最低 1）
        float FallbackRadius(const TransformComponent* transform)
        {
            if (!transform) {
                return 1.0f;
            }
            const Vector3& scale = transform->Get().GetScale();
            return (std::max)({ scale.x, scale.y, scale.z, 1.0f });
        }

        bool RaycastFallbackSphere(const Ray& ray, GameObject& object, float radius, Hit& hit)
        {
            // レイの始点を中に含む球は当てない（映しているカメラのオブジェクトが、どこを指しても当たってしまう）
            if (Distance(ray.origin, object.GetWorldPosition()) <= radius) {
                return false;
            }
            Geometry::RayHit sphereHit{};
            if (!Geometry::Raycast(Geometry::Ray{ ray.origin, ray.direction },
                                   Geometry::Sphere{ object.GetWorldPosition(), radius }, &sphereHit)) {
                return false;
            }
            hit.object = &object;
            hit.point = sphereHit.point;
            hit.normal = sphereHit.normal;
            hit.distance = sphereHit.distance;
            return true;
        }
    }

    Ray ScreenToRay(const Vector2& normalizedPosition, const Camera& camera)
    {
        Ray ray;
        ray.origin = camera.GetPosition();

        const float ndcX = normalizedPosition.x * 2.0f - 1.0f;
        const float ndcY = 1.0f - normalizedPosition.y * 2.0f;

        // 射影行列から画角と縦横比を読み、ビュー空間の向きを作る
        const Matrix4x4 projection = camera.GetProjectionMatrix();
        const Matrix4x4 view = camera.GetViewMatrix();
        const float tanHalfFovY = 1.0f / projection.m[1][1];
        const float aspectRatio = projection.m[1][1] / projection.m[0][0];
        const float viewX = ndcX * tanHalfFovY * aspectRatio;
        const float viewY = ndcY * tanHalfFovY;
        const float viewZ = 1.0f;

        // ビュー行列の回転部分の転置でワールドへ戻す
        const float worldX = viewX * view.m[0][0] + viewY * view.m[0][1] + viewZ * view.m[0][2];
        const float worldY = viewX * view.m[1][0] + viewY * view.m[1][1] + viewZ * view.m[1][2];
        const float worldZ = viewX * view.m[2][0] + viewY * view.m[2][1] + viewZ * view.m[2][2];
        const float length = std::sqrt(worldX * worldX + worldY * worldY + worldZ * worldZ);
        if (length > 0.0f) {
            ray.direction = Vector3(worldX / length, worldY / length, worldZ / length);
        }
        return ray;
    }

    bool RaycastObject(const Ray& ray, GameObject& object, Fallback fallback, Hit& hit)
    {
        auto* const renderer = object.GetComponent<MeshRendererComponent>();
        auto* const transformComponent = object.GetComponent<TransformComponent>();
        const auto useFallback = [&] {
            return fallback == Fallback::Sphere
                && RaycastFallbackSphere(ray, object, FallbackRadius(transformComponent), hit);
            };

        Model* const model = renderer ? renderer->GetModel() : nullptr;
        const ModelResource* const modelResource = (model && model->IsInitialized()) ? model->GetModelResource() : nullptr;
        if (!transformComponent || !modelResource || !modelResource->IsLoaded()) {
            return useFallback();
        }

        const ModelData& modelData = modelResource->GetModelData();
        const std::vector<VertexData>& vertices = modelData.vertices;
        const std::vector<int32_t>& indices = modelData.indices;
        if (vertices.empty() || indices.empty()) {
            return useFallback();
        }

        // 頂点を毎回ワールドへ移す代わりに、レイをローカル空間へ 1 回だけ移す。
        // ローカルの t はスケールで歪むので、距離はワールドへ戻してから比べる。
        const Matrix4x4 worldMatrix = transformComponent->Get().GetWorldMatrix();
        const Matrix4x4 inverseWorld = MathCore::Matrix::Inverse(worldMatrix);
        const Vector3 localOrigin = TransformPoint(ray.origin, inverseWorld);
        const Vector3 localDirection = TransformDirection(ray.direction, inverseWorld);
        const float localLengthSq = localDirection.x * localDirection.x + localDirection.y * localDirection.y
            + localDirection.z * localDirection.z;
        if (!(localLengthSq > 0.0f) || !std::isfinite(localLengthSq)) {
            // スケール 0 などで逆行列が作れないときは球で判定する
            return useFallback();
        }

        const Geometry::Ray localRay{ localOrigin, localDirection };
        const BoundingBox& localBounds = modelResource->GetLocalBoundingBox();
        if (localBounds.IsValid() && !Geometry::Raycast(localRay, localBounds)) {
            return false;
        }

        const int32_t vertexCount = static_cast<int32_t>(vertices.size());
        float closestT = (std::numeric_limits<float>::max)();
        Vector3 closestNormal{ 0.0f, 1.0f, 0.0f };
        bool found = false;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const int32_t i0 = indices[i];
            const int32_t i1 = indices[i + 1];
            const int32_t i2 = indices[i + 2];
            if (i0 < 0 || i0 >= vertexCount || i1 < 0 || i1 >= vertexCount || i2 < 0 || i2 >= vertexCount) {
                continue;
            }
            const auto& p0 = vertices[i0].position;
            const auto& p1 = vertices[i1].position;
            const auto& p2 = vertices[i2].position;
            Geometry::RayHit triangleHit{};
            if (Geometry::RaycastTriangle(localRay, Vector3(p0.x, p0.y, p0.z), Vector3(p1.x, p1.y, p1.z),
                                          Vector3(p2.x, p2.y, p2.z), &triangleHit, 0.0f, closestT)) {
                closestT = triangleHit.distance;
                closestNormal = triangleHit.normal;
                found = true;
            }
        }
        if (!found) {
            return false;
        }

        const Vector3 localHit(localOrigin.x + localDirection.x * closestT, localOrigin.y + localDirection.y * closestT,
            localOrigin.z + localDirection.z * closestT);
        hit.object = &object;
        hit.point = TransformPoint(localHit, worldMatrix);
        hit.normal = TransformNormal(closestNormal, inverseWorld);
        hit.distance = Distance(ray.origin, hit.point);
        return true;
    }

    std::optional<Hit> Raycast(const GameObjectManager& objects, const Ray& ray, Fallback fallback)
    {
        std::optional<Hit> closest;
        for (const auto& object : objects.GetAllObjects()) {
            if (!object || !object->IsActive() || !objects.IsShownInIsolation(*object)) {
                continue;
            }

            // 3D の位置を持たないもの（UI・空・管理用のオブジェクト）は当てない
            if (!object->GetComponent<ITransformSource>()) {
                continue;
            }

            Hit hit;
            if (RaycastObject(ray, *object, fallback, hit) && (!closest || hit.distance < closest->distance)) {
                closest = hit;
            }
        }
        return closest;
    }
}

#endif // CORE_EDITOR
