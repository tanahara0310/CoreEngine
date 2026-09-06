#pragma once

#include "../BaseRenderer.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class ParticleSystem;
    class Camera;
    class ResourceFactory;
    class ShaderReflectionData;
}

namespace CoreEngine
{
/// @brief パーティクルレンダラーの基底クラス
/// 共通の処理をまとめ、派生クラスで描画方法のみを実装
class BaseParticleRenderer : public BaseRenderer {
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
    void SetCamera(const CoreEngine::Camera* camera) override;

    /// @brief 今フレームのフォグ定数を受け取る（gFog）
    /// @details FogPass が毎フレーム供給する。パーティクルは出力がアルファ事前乗算の
    ///          加算合成なので、内散乱を足さない「減衰のみ」バリアントを渡すこと。
    void SetFogConstants(D3D12_GPU_VIRTUAL_ADDRESS attenuationOnly) { fogCBV_ = attenuationOnly; }

    /// @brief ResourceFactoryを設定（初期化前に呼び出す必要がある）
    /// @param resourceFactory リソースファクトリ
    void SetResourceFactory(ResourceFactory* resourceFactory) { resourceFactory_ = resourceFactory; }

    /// @brief パーティクルシステムを描画（派生クラスで実装）
    /// @param particle パーティクルシステム
    virtual void Draw(CoreEngine::ParticleSystem* particle) = 0;

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;

protected:
    /// @brief 今フレームのフォグ定数（0 = 未供給で差さない）
    D3D12_GPU_VIRTUAL_ADDRESS fogCBV_ = 0;

    // ──────────────────────────────────────────────────────────
    // 共通リソース
    // ──────────────────────────────────────────────────────────
    
    CoreEngine::ResourceFactory* resourceFactory_ = nullptr;
    ID3D12Device* device_ = nullptr;
    ID3D12GraphicsCommandList* cmdList_ = nullptr;
    const CoreEngine::Camera* camera_ = nullptr;

    // BaseRenderer から継承したサブシステムを使用（pipelineMg_, rootSignatureMg_, shaderCompiler_, reflectionBuilder_ は削除）

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

    // ──────────────────────────────────────────────────────────
    // ルートシグネチャの元になるシェーダー
    // ──────────────────────────────────────────────────────────

    /// @brief ルートシグネチャ構築に使う頂点シェーダーのパス
    /// @details CreateRootSignature がこれをコンパイルしてリフレクションを取る。
    ///          自前のシェーダーを持つ派生クラスはここを差し替えることで、
    ///          そのシェーダーが宣言したリソースだけを含む RS が組まれる。
    virtual const wchar_t* GetVertexShaderPath() const {
        return L"Engine/Assets/Shaders/Particle/Particle.VS.hlsl";
    }

    /// @brief ルートシグネチャ構築に使うピクセルシェーダーのパス
    virtual const wchar_t* GetPixelShaderPath() const {
        return L"Engine/Assets/Shaders/Particle/Particle.PS.hlsl";
    }
};
}
