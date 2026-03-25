#pragma once

#include "ObjectCommon/GameObject.h"
#include "Math/MathCore.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Material/SkyBoxMaterialInstance.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <memory>

namespace CoreEngine {
    class SkyBoxRenderer;
}

/// @brief SkyBoxオブジェクト
/// @note ModelGameObject は使用しない。model_ を持たず、独自の頂点バッファと定数バッファで描画する。
class SkyBoxObject : public CoreEngine::GameObject {
public:
    SkyBoxObject() = default;
    ~SkyBoxObject() override = default;

    /// @brief SkyBoxRendererを設定（ルートパラメータインデックス取得用）
    static void SetSkyBoxRenderer(CoreEngine::SkyBoxRenderer* renderer);

    /// @brief 頂点バッファ・マテリアル・定数バッファを構築する
    void Initialize() override;

    /// @brief IBL回転を毎フレーム RenderManager に伝播する
    void Update() override;

    /// @brief 描画
    void Draw(const CoreEngine::ICamera* camera) override;

#ifdef _DEBUG
    /// @brief 拡張ImGuiデバッグUI描画（Transform + SkyBox固有パラメータ）
    bool DrawImGuiExtended() override;
#endif

    const char* GetObjectName() const override { return "SkyBox"; }
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkyBox; }
    CoreEngine::Vector3 GetWorldPosition() const override { return transform_.translate; }

    /// @brief キューブマップテクスチャを設定する
    void SetTexture(const CoreEngine::TextureManager::LoadedTexture& texture) { texture_ = texture; }

    /// @brief Y軸回転を取得（IBL回転確認用）
    float GetRotationY() const { return transform_.rotate.y; }

    /// @brief マテリアルインスタンスを直接取得
    CoreEngine::IMaterial* GetMaterial() { return material_.get(); }

private:
    void CreateBoxVertices();
    void CreateTransformBuffer();

    /// @brief トランスフォーム（EulerTransform: translate/rotate/scale のみ。GPU行列バッファは持たない）
    CoreEngine::EulerTransform transform_;

    /// @brief キューブマップテクスチャ（SetTexture() で外部から設定する）
    CoreEngine::TextureManager::LoadedTexture texture_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    std::unique_ptr<CoreEngine::SkyBoxMaterialInstance> material_;

    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;

    struct TransformationMatrix {
        CoreEngine::Matrix4x4 WVP;
    };
    TransformationMatrix* transformData_ = nullptr;

    static constexpr UINT kVertexCount = 24;
    static constexpr UINT kIndexCount  = 36;
};

