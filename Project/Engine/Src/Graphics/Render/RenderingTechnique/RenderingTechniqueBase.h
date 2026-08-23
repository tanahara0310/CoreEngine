#pragma once
#include <d3d12.h>
#include <string>
#include <wrl.h>
#include <memory>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
// 前方宣言
class ShaderReflectionData;
struct RenderContext;

/// @note 「フレームオーバーラップ対応の定数バッファ」はかつて FrameRingConstantBuffer として
///       このヘッダに置かれていたが、同じ問題（CPU が GPU の 1 フレーム先を走るので
///       単一バッファを毎フレーム上書きすると実行中の値を書き潰す）はレンダリング技術に
///       限らないため、基盤層の UploadRing へ一般化した。
///       使い方: `dxCommon->GetUploadRing().AllocateConstants(params_)`

/// @brief レンダリング技術基底クラス
/// @details SSAO、TAA、SSRなどの高度なレンダリング技術の基底クラス
///          PostEffectBase とは異なり、GBuffer・深度・Historyバッファなど
///          複数の入力リソースやフレーム間状態を扱う技術に特化
class RenderingTechniqueBase {
public:
    virtual ~RenderingTechniqueBase() = default;

    /// @brief 初期化
    /// @param dxCommon GraphicsCore
    virtual void Initialize(GraphicsCore* dxCommon);

    /// @brief レンダリング技術の実行
    /// @param context レンダリングコンテキスト（GBuffer、深度、カメラなどへのアクセス）
    /// @param outputSrvHandle 出力先のSRVハンドル（次のパスへの受け渡し用）
    virtual void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) = 0;

    /// @brief リサイズ時の処理
    /// @param width 新しい幅
    /// @param height 新しい高さ
    virtual void OnResize([[maybe_unused]] uint32_t width, [[maybe_unused]] uint32_t height) {}

    /// @brief ImGuiでパラメータを調整する関数
    virtual void DrawImGui() {}

    /// @brief 更新処理（デフォルトは空実装）
    /// @param deltaTime フレーム時間
    virtual void Update(float /*deltaTime*/) {}

    /// @brief 技術の有効/無効を設定
    /// @param enabled 有効にするかどうか
    virtual void SetEnabled(bool enabled)
    {
        if (CVar<bool>* cvar = GetEnabledCVar()) {
            cvar->Set(enabled);
            return;
        }
        enabled_ = enabled;
    }

    /// @brief 技術が有効かどうかを取得
    /// @return 有効ならtrue
    bool IsEnabled() const
    {
        if (const CVar<bool>* cvar = GetEnabledCVar()) {
            return cvar->Get();
        }
        return enabled_;
    }

    /// @brief 常時有効な技術かどうかを取得（無効化不可）
    /// @return 常時有効ならtrue
    virtual bool IsAlwaysEnabled() const { return false; }

    /// @brief 有効/無効を保持する CVar を返す
    /// @return CVar（"r.<Technique>.Enabled"）。持たない技術は nullptr
    /// @details 派生が自分のファイルスコープ CVar を返すことで、有効状態が
    ///          自動的に CVars.json へ保存される。常時有効な技術は nullptr のままでよい。
    ///          設計: Docs/Engine/Editor/CVar_Design.md
    virtual CVar<bool>* GetEnabledCVar() const { return nullptr; }

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;

protected:
    /// @brief 技術名を取得（デバッグ用）
    virtual std::string GetTechniqueName() const = 0;

    /// @brief ピクセルシェーダーパスを取得（オプション）
    /// @details Compute Shader のみの場合は空文字列を返す
    virtual const std::wstring& GetPixelShaderPath() const { return emptyPath_; }

    /// @brief 頂点シェーダーパスを取得（オプション）
    /// @details デフォルトはフルスクリーンクアッド用
    virtual const std::wstring& GetVertexShaderPath() const;

    /// @brief Compute Shaderパスを取得（オプション）
    /// @details Compute Shader を使わない場合は空文字列を返す
    virtual const std::wstring& GetComputeShaderPath() const { return emptyPath_; }

    /// @brief ルートシグネチャ構築前に呼ばれる設定フック
    /// @details サブクラスでオーバーライドすることで追加サンプラーなどを設定できる
    /// @param config 構築中のルートシグネチャ設定
    virtual void OnConfigureRootSignature(RootSignatureConfig& /*config*/) {}

    /// @brief シェーダーの種類を取得（Graphics or Compute）
    /// @return Compute Shader を使う場合true
    virtual bool IsComputeShader() const { return !GetComputeShaderPath().empty(); }

    /// @brief フルスクリーンクアッドを描画する共通処理
    /// @param commandList コマンドリスト
    void DrawFullscreenQuad(ID3D12GraphicsCommandList* commandList);

protected:
    GraphicsCore* graphicsCore_ = nullptr;

    // Graphics Shader用
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob_;
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;

    // Compute Shader用
    Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob_;

    std::unique_ptr<RootSignatureManager> rootSignatureManager_;
    PipelineStateManager pipelineStateManager_;

    // シェーダーリフレクションデータ
    std::unique_ptr<ShaderReflectionData> reflectionData_;

    bool enabled_ = true; // デフォルトで有効

private:
    static std::wstring emptyPath_;
};
}
