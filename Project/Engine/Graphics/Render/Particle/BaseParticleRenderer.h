#pragma once

#include "../IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflectionBuilder.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class ParticleSystem;
    class ICamera;
    class ResourceFactory;
    class ShaderReflectionData;
}

/// @brief パーティクルレンダラーの基底クラス
/// 共通の処理をまとめ、派生クラスで描画方法のみを実装

namespace CoreEngine
{
class BaseParticleRenderer : public IRenderer {
public:
    BaseParticleRenderer() = default;
    ~BaseParticleRenderer() override = default;

    /// @brief 初期化（共通処理）
    /// @param device D3D12デバイス
    void Initialize(ID3D12Device* device) override;

    /// @brief 描画パスの開始（共通処理）
    /// @param cmdList コマンドリスト
    /// @param blendMode ブレンドモード
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode = BlendMode::kBlendModeNone) override;

    /// @brief 描画パスの終了（共通処理）
    void EndPass() override;

    /// @brief カメラを設定
    /// @param camera カメラオブジェクト
    void SetCamera(const CoreEngine::ICamera* camera) override;

    /// @brief ResourceFactoryを設定（初期化前に呼び出す必要がある）
    /// @param resourceFactory リソースファクトリ
    void SetResourceFactory(ResourceFactory* resourceFactory) { resourceFactory_ = resourceFactory; }

    /// @brief パーティクルシステムを描画（派生クラスで実装）
    /// @param particle パーティクルシステム
    virtual void Draw(CoreEngine::ParticleSystem* particle) = 0;

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;

protected:
    // ──────────────────────────────────────────────────────────
    // 共通リソース
    // ──────────────────────────────────────────────────────────
    
    CoreEngine::ResourceFactory* resourceFactory_ = nullptr;
    ID3D12Device* device_ = nullptr;
    ID3D12GraphicsCommandList* cmdList_ = nullptr;
    const CoreEngine::ICamera* camera_ = nullptr;

    // パイプラインとシェーダー
    std::unique_ptr<PipelineStateManager> pipelineMg_;
    std::unique_ptr<RootSignatureManager> rootSignatureMg_;
    std::unique_ptr<ShaderCompiler> shaderCompiler_;
    std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;

    // シェーダーリフレクションデータ
    std::unique_ptr<ShaderReflectionData> reflectionData_;

    // ──────────────────────────────────────────────────────────
    // 共通処理メソッド
    // ──────────────────────────────────────────────────────────

    /// @brief ルートシグネチャの作成（共通実装）
    void CreateRootSignature();

    /// @brief 基本的な検証を行う
    /// @param particle パーティクルシステム
    /// @return 描画可能な場合true
    bool ValidateDrawCall(CoreEngine::ParticleSystem* particle) const;

    /// @brief 共通のリソース設定を行う
    /// @param particle パーティクルシステム
    /// @param textureHandle テクスチャハンドル
    void SetupCommonResources(CoreEngine::ParticleSystem* particle, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle);

    // ──────────────────────────────────────────────────────────
    // 派生クラスで実装すべき純粋仮想関数
    // ──────────────────────────────────────────────────────────

    /// @brief パイプラインステートオブジェクトの作成（派生クラスで実装）
    virtual void CreatePSO() = 0;

    /// @brief BeginPassでの追加処理（派生クラスでオーバーライド可能）
    virtual void OnBeginPass() {}
};
}
