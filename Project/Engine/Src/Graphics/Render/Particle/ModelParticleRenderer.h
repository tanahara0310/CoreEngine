#pragma once

#include "BaseParticleRenderer.h"
#include "Graphics/RootSignature/RootSlot.h"
#include <d3d12.h>
#include <wrl.h>

// 前方宣言
class ParticleSystem;

namespace CoreEngine
{
// 前方宣言
class LightManager;

/// @brief モデルパーティクル専用レンダラー
/// @details 板ポリパーティクルと違い形状由来の法線を持つので、ディレクショナルライトで
///          陰影を付けて立体感を出す。そのために板ポリとは別のピクセルシェーダーを使う。
class ModelParticleRenderer : public BaseParticleRenderer {
public:
    ModelParticleRenderer() = default;
    ~ModelParticleRenderer() override = default;

    /// @brief 初期化（共通処理 + ライト用ルートスロットの解決）
    /// @param device D3D12デバイス
    void Initialize(ID3D12Device* device) override;

    /// @brief このレンダラーがサポートする描画タイプを取得
    /// @return 描画パスタイプ
    RenderPassType GetRenderPassType() const override { return RenderPassType::ModelParticle; }

    /// @brief モデルパーティクルを描画
    /// @param particle パーティクルシステム
    void Draw(ParticleSystem* particle) override;

    /// @brief ライトマネージャーを設定
    /// @param lightManager ライトマネージャー（未設定ならアンリットのまま描画される）
    void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }

protected:
    /// @brief パイプラインステートオブジェクトの作成（モデル用）
    void CreatePSO() override;

    /// @brief パス開始時にディレクショナルライトを差す
    void OnBeginPass() override;

    const wchar_t* GetVertexShaderPath() const override {
        return L"Engine/Assets/Shaders/Particle/ModelParticle.VS.hlsl";
    }

    /// @note 板ポリ用の Particle.PS.hlsl と違い、こちらはライティングを行う
    const wchar_t* GetPixelShaderPath() const override {
        return L"Engine/Assets/Shaders/Particle/ModelParticle.PS.hlsl";
    }

private:
    LightManager* lightManager_ = nullptr;

    // 初期化時に解決するルートパラメータ（描画中に名前で引かない）
    RootSlot lightCountsSlot_;
    RootSlot directionalLightsSlot_;
};
}
