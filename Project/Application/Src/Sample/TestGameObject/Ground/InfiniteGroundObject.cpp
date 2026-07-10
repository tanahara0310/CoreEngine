#include "pch.h"
#include "InfiniteGroundObject.h"

#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialInstance.h"

#include <cmath>
#include <cstdio>

using namespace CoreEngine;

namespace {

    /// @brief 無限床専用の平面メッシュ生成器
    ///
    /// PlaneMeshGenerator との違いは UV の焼き込み方。
    /// UV を 0..1 ではなく「ローカル座標 / tileWorldSize」として持たせる。
    /// - 平面中心（＝追従するカメラの真下）が UV 原点になるため、
    ///   カメラ近傍の頂点 UV は絶対値が小さく、補間精度が落ちない。
    /// - サンプラーが WRAP なので、UV が 1 進むごとにテクスチャ 1 周
    ///   （= tileWorldSize メートル）繰り返される。
    class InfiniteGroundMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        InfiniteGroundMeshGenerator(float halfExtent, uint32_t subdivisions, float tileWorldSize)
            : halfExtent_(halfExtent), subdivisions_(subdivisions), tileWorldSize_(tileWorldSize) {
        }

        ModelData Generate() const override {
            ModelData data;

            const uint32_t verts = subdivisions_ + 1;
            const float size = halfExtent_ * 2.0f;
            const float step = size / static_cast<float>(subdivisions_);

            data.vertices.reserve(static_cast<size_t>(verts) * verts);
            for (uint32_t z = 0; z < verts; ++z) {
                const float localZ = -halfExtent_ + step * static_cast<float>(z);
                for (uint32_t x = 0; x < verts; ++x) {
                    const float localX = -halfExtent_ + step * static_cast<float>(x);

                    VertexData vertex{};
                    vertex.position = { localX, 0.0f, localZ, 1.0f };
                    vertex.texcoord = { localX / tileWorldSize_, localZ / tileWorldSize_ };
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                    vertex.tangent = { 1.0f, 0.0f, 0.0f };
                    data.vertices.push_back(vertex);
                }
            }

            data.indices.reserve(static_cast<size_t>(subdivisions_) * subdivisions_ * 6);
            for (uint32_t z = 0; z < subdivisions_; ++z) {
                for (uint32_t x = 0; x < subdivisions_; ++x) {
                    const uint32_t topLeft = z * verts + x;
                    const uint32_t topRight = topLeft + 1;
                    const uint32_t bottomLeft = topLeft + verts;
                    const uint32_t bottomRight = bottomLeft + 1;

                    data.indices.push_back(static_cast<int32_t>(topLeft));
                    data.indices.push_back(static_cast<int32_t>(bottomLeft));
                    data.indices.push_back(static_cast<int32_t>(topRight));

                    data.indices.push_back(static_cast<int32_t>(topRight));
                    data.indices.push_back(static_cast<int32_t>(bottomLeft));
                    data.indices.push_back(static_cast<int32_t>(bottomRight));
                }
            }

            SubMeshData subMesh{};
            subMesh.name = "InfiniteGround";
            subMesh.startIndex = 0;
            subMesh.indexCount = static_cast<uint32_t>(data.indices.size());
            subMesh.materialIndex = 0;
            data.subMeshes.push_back(subMesh);

            MaterialAsset mat{};
            mat.name = "Default";
            data.materials.push_back(mat);

            data.rootNode.name = "root";
            data.rootNode.localMatrix = MathCore::Matrix::Identity();
            data.rootNode.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };

            return data;
        }

        std::string GetCacheKey() const override {
            char buf[128];
            snprintf(buf, sizeof(buf), "Primitive::InfiniteGround_%.1f_%u_%.2f",
                     halfExtent_, subdivisions_, tileWorldSize_);
            return buf;
        }

    private:
        float    halfExtent_;
        uint32_t subdivisions_;
        float    tileWorldSize_;
    };

} // namespace

const char* InfiniteGroundObject::GetObjectName() const {
    return "InfiniteGround";
}

std::string InfiniteGroundObject::GetTexturePath() const {
    return "tile_white.png";
}

std::unique_ptr<IPrimitiveMeshGenerator> InfiniteGroundObject::CreateMeshGenerator() const {
    return std::make_unique<InfiniteGroundMeshGenerator>(kHalfExtent, kSubdivisions, kTileWorldSize);
}

void InfiniteGroundObject::OnInitialize() {
    if (auto* model = GetModel()) {
        if (auto* mat = model->GetMaterial()) {
            // テクスチャの階調（グレー市松）をそのまま活かす。
            // UV はメッシュに焼き込み済みのため uvTransform は既定（単位行列）のまま使う。
            mat->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            mat->SetMetallic(0.0f);
            mat->SetRoughness(0.9f);
            mat->SetLightingEnabled(true);
            // IBL は使わず PBR 既定アンビエントを使う。
            // 本オブジェクトは全シーン共通の既定床であり、IBL 未セットアップのシーンでも
            // アンビエントが黒つぶれしないようにするため。
            mat->SetIBLEnabled(false);
        }
    }

    // 既定床はシーンデータ（JSON）に保存しない
    SetSerializeEnabled(false);

    transform_.translate = { 0.0f, kGroundHeight, 0.0f };
}

void InfiniteGroundObject::FollowCamera(const Vector3& cameraPosition) {
    // カメラ XZ に追従。ただしタイル周期（kTileWorldSize）にスナップすることで
    // タイル模様をワールドに固定する。
    //
    // メッシュ UV は「ローカル座標 / kTileWorldSize」なので、
    //   uv = (world - translate) / kTileWorldSize
    // translate が kTileWorldSize の整数倍であれば uv は world / kTileWorldSize と
    // 周期 1（＝テクスチャ 1 周）の差しか持たない。WRAP サンプリングでは同一の
    // テクセルを引くため、模様はワールドに固定され、追従移動は不可視になる。
    auto snap = [](float value, float period) {
        return std::round(value / period) * period;
    };
    transform_.translate.x = snap(cameraPosition.x, kTileWorldSize);
    transform_.translate.z = snap(cameraPosition.z, kTileWorldSize);
    transform_.translate.y = kGroundHeight;
}
