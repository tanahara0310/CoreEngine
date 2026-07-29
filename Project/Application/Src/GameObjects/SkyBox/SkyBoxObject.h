#pragma once

#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <array>
#include <memory>
#include <string>

namespace CoreEngine {
    class SkyBoxRenderer;
}

/// @brief 空（大気散乱）オブジェクト
/// @note ModelGameObject は使用しない。model_ を持たず、独自の頂点バッファと定数バッファで描画する。
///       背景は常に大気散乱（SkyAtmosphere.PS.hlsl）で描く。静的 HDR キューブマップ経路は廃止済み。
class SkyBoxObject : public CoreEngine::GameObject {
public:
    SkyBoxObject() = default;
    ~SkyBoxObject() override = default;

    /// @brief SkyBoxRendererを設定（ルートパラメータインデックス取得用）
    static void SetSkyBoxRenderer(CoreEngine::SkyBoxRenderer* renderer);

    /// @brief 頂点バッファ・定数バッファを構築する
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
#endif

    const char* GetObjectName() const override { return "SkyBox"; }
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkyBox; }
    CoreEngine::Vector3 GetWorldPosition() const override { return transform_.translate; }

    /// @brief Y軸回転を取得（IBL回転確認用）
    float GetRotationY() const { return transform_.rotate.y; }

    /// @brief 環境輝度スケールを取得（IBL の映り込み強度と連動）
    float GetEnvironmentIntensity() const { return environmentIntensity_; }

    /// @brief 環境輝度スケールを設定
    void SetEnvironmentIntensity(float intensity) { environmentIntensity_ = intensity; }

private:
    void CreateBoxVertices();
    void CreateTransformBuffer();

    /// @brief トランスフォーム（EulerTransform: translate/rotate/scale のみ。GPU行列バッファは持たない）
    CoreEngine::EulerTransform transform_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    struct TransformationMatrix {
        CoreEngine::Matrix4x4 WVP;
    };

    static constexpr UINT kTransformBufferCount = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTransformBufferCount> transformBuffers_{};
    std::array<TransformationMatrix*, kTransformBufferCount> transformData_{};
    UINT transformBufferIndex_ = 0;

    static constexpr UINT kVertexCount = 24;
    static constexpr UINT kIndexCount  = 36;

    /// @brief 環境輝度スケール（IBL の映り込み強度へ伝播）
    float environmentIntensity_ = 1.0f;
};

