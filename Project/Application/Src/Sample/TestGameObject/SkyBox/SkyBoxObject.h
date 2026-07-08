#pragma once

#include "ObjectCommon/GameObject.h"
#include "Math/MathCore.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Material/SkyBoxMaterialInstance.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <array>
#include <memory>
#include <string>

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
    /// @brief インスペクタータブ定義を返す
    int GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;

    /// @brief 指定タブのコンテンツを描画する
    bool DrawInspectorTabContent(int tabIndex) override;

    /// @brief トランスフォームタブ描画
    bool DrawTransformSection();

    /// @brief テクスチャタブ描画
    bool DrawTextureSection();
#endif

    const char* GetObjectName() const override { return "SkyBox"; }
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkyBox; }
    CoreEngine::Vector3 GetWorldPosition() const override { return transform_.translate; }

    /// @brief キューブマップテクスチャを設定し、必要に応じて IBL を再構築する
    /// @param texture 設定するキューブマップテクスチャ
    /// @param textureKey IBL キャッシュ識別キー（空文字列時は内部で自動生成）
    void SetTexture(const CoreEngine::TextureManager::LoadedTexture& texture, const std::string& textureKey = "");

    /// @brief Y軸回転を取得（IBL回転確認用）
    float GetRotationY() const { return transform_.rotate.y; }

    /// @brief マテリアルインスタンスを直接取得
    CoreEngine::IMaterial* GetMaterial() { return material_.get(); }

    /// @brief 大気散乱モードの有効/無効を設定
    /// @details 有効時はキューブマップの代わりに大気散乱（SkyAtmosphere.PS.hlsl）で空を描画する。
    ///          テクスチャ設定は不要になる。
    void SetAtmosphereMode(bool enabled) { atmosphereMode_ = enabled; }

    /// @brief 大気散乱モードが有効か
    bool IsAtmosphereMode() const { return atmosphereMode_; }

private:
    void CreateBoxVertices();
    void CreateTransformBuffer();
    void UpdateIBLFromTexture(const std::string& textureKey, bool forceRegenerate);

    /// @brief トランスフォーム（EulerTransform: translate/rotate/scale のみ。GPU行列バッファは持たない）
    CoreEngine::EulerTransform transform_;

    /// @brief キューブマップテクスチャ（SetTexture() で外部から設定する）
    CoreEngine::TextureManager::LoadedTexture texture_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    std::unique_ptr<CoreEngine::SkyBoxMaterialInstance> material_;

    struct TransformationMatrix {
        CoreEngine::Matrix4x4 WVP;
    };

    static constexpr UINT kTransformBufferCount = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTransformBufferCount> transformBuffers_{};
    std::array<TransformationMatrix*, kTransformBufferCount> transformData_{};
    UINT transformBufferIndex_ = 0;

    static constexpr UINT kVertexCount = 24;
    static constexpr UINT kIndexCount  = 36;

    /// @brief テクスチャ名（インスペクター表示用）
    std::string textureName_;

    /// @brief 大気散乱モード（true でキューブマップの代わりに大気散乱で空を描く）
    bool atmosphereMode_ = false;

#ifdef _DEBUG
    /// @brief 非HDRファイルドロップ時の警告メッセージ
    std::string dropWarning_;
#endif
};

