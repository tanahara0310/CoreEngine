#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include <wrl/client.h>
#include <d3d12.h>

namespace CoreEngine {
    class SkyBoxRenderer;
}

/// @brief SkyBoxオブジェクト
class SkyBoxObject : public CoreEngine::GameObject {
public:
    /// @brief コンストラクタ
    SkyBoxObject() = default;

    /// @brief デストラクタ
    ~SkyBoxObject() override = default;

    /// @brief SkyBoxRendererを設定（ルートパラメータインデックス取得用）
    /// @param renderer SkyBoxRendererのポインタ
    static void SetSkyBoxRenderer(CoreEngine::SkyBoxRenderer* renderer);

    /// @brief 初期化
    /// @param engine エンジンシステム
    void Initialize();

    /// @brief 更新
    void Update() override;

    /// @brief 描画
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

#ifdef _DEBUG
    /// @brief 拡張ImGuiデバッグUI描画（SkyBox固有パラメータ）
    /// @return 変更があった場合 true
    bool DrawImGuiExtended() override;

    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "SkyBox"; }
#endif

    /// @brief このオブジェクトの描画パスタイプを取得
    /// @return 描画パスタイプ（SkyBox）
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkyBox; }

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief テクスチャを設定
    /// @param texture 設定するテクスチャ
    void SetTexture(const CoreEngine::TextureManager::LoadedTexture& texture) { texture_ = texture; }

    /// @brief Y軸回転を取得（ラジアン）
    /// @return Y軸回転角度
    float GetRotationY() const { return transform_.rotate.y; }

private:
    /// @brief 箱の頂点データを生成
    void CreateBoxVertices();

    /// @brief マテリアル用定数バッファを作成
    void CreateMaterialBuffer();

    /// @brief トランスフォーム用定数バッファを作成
    void CreateTransformBuffer();

    /// @brief 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;

    /// @brief 頂点バッファビュー
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    /// @brief インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;

    /// @brief インデックスバッファビュー
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    /// @brief マテリアル用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer_;

    /// @brief トランスフォーム用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;

    /// @brief マテリアルデータ
    struct SkyBoxMaterial {
        CoreEngine::Vector4 color;
    };
    SkyBoxMaterial* materialData_ = nullptr;

    /// @brief トランスフォームデータ
    struct TransformationMatrix {
        CoreEngine::Matrix4x4 WVP;
    };
    TransformationMatrix* transformData_ = nullptr;

    /// @brief 頂点数
    static constexpr UINT kVertexCount = 24;

    /// @brief インデックス数
    static constexpr UINT kIndexCount = 36;
};

