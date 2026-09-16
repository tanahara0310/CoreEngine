#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Math/MathCore.h"

#include <array>
#include <d3d12.h>
#include <wrl/client.h>

namespace CoreEngine
{
class SkyBoxRenderer;

/// @brief 空（大気散乱）を描くコンポーネント
/// @details 内向きの箱を独自の頂点バッファで描き、色は大気散乱（SkyAtmosphere.PS.hlsl）で決まる。
///          回転と環境光の強さは、IBL の回転と強さとして毎フレーム RenderManager へ渡す。
/// @note 空は EnvironmentFeature がシーンに 1 つ作る（保存しない）ので、ファクトリには載せない。
class SkyBoxComponent : public IComponent, public IRenderableComponent {
public:
    SkyBoxComponent() = default;
    ~SkyBoxComponent() override;

    const char* GetTypeName() const override { return "SkyBox"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "スカイボックス"; }
#endif

    /// @brief 頂点・インデックス・定数バッファを作り、レンダラーを引く
    void Awake() override;

    /// @brief 回転と環境光の強さを RenderManager へ渡す
    void Update() override;

    // ===== IRenderableComponent =====

    RenderPassType GetRenderPassType() const override { return RenderPassType::SkyBox; }

    /// @brief 平行移動を除いたビューで箱を描き、大気散乱の LUT を差す
    void Render(const DrawViewInfo& view) override;

    // ===== 値 =====

    /// @brief 空と環境光（IBL）の向き（ラジアン）
    const Vector3& GetRotation() const { return rotation_; }
    void SetRotation(const Vector3& radians) { rotation_ = radians; }

    /// @brief 環境光の強さ（IBL の映り込みの強さ）
    float GetEnvironmentIntensity() const { return environmentIntensity_; }
    void SetEnvironmentIntensity(float intensity) { environmentIntensity_ = intensity; }

private:
    /// @brief 内向きの箱の頂点・インデックスバッファを作る
    void CreateBoxBuffers(ID3D12Device* device);

    /// @brief ビューごとの変換行列の定数バッファを作る
    void CreateTransformBuffers(ID3D12Device* device);

    struct TransformationMatrix {
        Matrix4x4 WVP;
    };

    static constexpr UINT kTransformBufferCount = 4;
    static constexpr UINT kVertexCount = 24;
    static constexpr UINT kIndexCount = 36;

    SkyBoxRenderer* renderer_ = nullptr;

    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };
    float environmentIntensity_ = 1.0f;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    /// 同じフレームに複数のビューで描くので、ビューごとに別の定数バッファを巡回して使う
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTransformBufferCount> transformBuffers_{};
    std::array<TransformationMatrix*, kTransformBufferCount> transformData_{};
    UINT transformBufferIndex_ = 0;
};
}
