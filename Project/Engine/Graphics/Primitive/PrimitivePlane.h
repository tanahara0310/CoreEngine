#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include "MathCore.h"

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class ResourceFactory;
}

/// @brief プリミティブな平面ジオメトリ
/// @details 頂点を直接指定して描画する平面（丸影などに使用）

namespace CoreEngine
{
class PrimitivePlane {
public:
    /// @brief 頂点データ構造体
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    PrimitivePlane() = default;
    ~PrimitivePlane() = default;

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    /// @param resourceFactory リソースファクトリ
    /// @param width 幅
    /// @param height 高さ（奥行き方向）
    void Initialize(CoreEngine::DirectXCommon* dxCommon, CoreEngine::ResourceFactory* resourceFactory, 
                    float width = 1.0f, float height = 1.0f);

    /// @brief 頂点バッファビューを取得
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }

    /// @brief インデックスバッファビューを取得
    const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const { return indexBufferView_; }

    /// @brief インデックス数を取得
    UINT GetIndexCount() const { return indexCount_; }

    /// @brief サイズを変更
    /// @param width 幅
    /// @param height 高さ
    void Resize(float width, float height);

private:
    /// @brief 頂点データを生成
    void CreateVertices(float width, float height);

    /// @brief バッファを作成
    void CreateBuffers();

    CoreEngine::DirectXCommon* dxCommon_ = nullptr;
    CoreEngine::ResourceFactory* resourceFactory_ = nullptr;

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // 頂点データ（マップ済みポインタ）
    VertexData* mappedVertexData_ = nullptr;

    // ジオメトリ情報
    UINT vertexCount_ = 4;
    UINT indexCount_ = 6;
    float currentWidth_ = 1.0f;
    float currentHeight_ = 1.0f;
};
}
