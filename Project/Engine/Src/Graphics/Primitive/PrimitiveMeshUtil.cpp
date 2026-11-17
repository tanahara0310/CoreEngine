#include "pch.h"
#include "PrimitiveMeshUtil.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    namespace PrimitiveMeshUtil
    {
        VertexData MakeVertex(const Vector4& position, const Vector2& texcoord,
            const Vector3& normal, const Vector3& tangent)
        {
            VertexData vertex{};
            vertex.position = position;
            vertex.texcoord = texcoord;
            vertex.normal = normal;
            vertex.tangent = tangent;
            return vertex;
        }

        void PushTriangle(ModelData& data, uint32_t i0, uint32_t i1, uint32_t i2)
        {
            data.indices.push_back(static_cast<int32_t>(i0));
            data.indices.push_back(static_cast<int32_t>(i1));
            data.indices.push_back(static_cast<int32_t>(i2));
        }

        void FinishSingleMesh(ModelData& data, const char* subMeshName)
        {
            SubMeshData subMesh{};
            subMesh.name = subMeshName;
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
        }
    }
}
