#pragma once
#include <d3d12.h>
#include <string>
#include <wrl.h>
#include <memory>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"

namespace CoreEngine
{
// 前方宣言
class ShaderReflectionData;
struct RenderContext;

/// @brief フレームオーバーラップ対応の定数バッファリング
/// @details このエンジンはフレーム N の GPU 実行中に CPU がフレーム N+1 を記録する。
///          Map しっぱなしの単一バッファを毎フレーム上書きすると、GPU が実行中フレームの
///          値を CPU が先に書き潰す（TAA ジッタ入り行列のように毎フレーム値が変わる
///          定数で実害が出る。SSAO は深度バッファと行列が食い違い AO がちらつく）。
///          Model の transformBuffers_[3] と同じ考え方の汎用小型版で、
///          フレームバッファリング数ぶんのスライスを持ち、記録中フレームのスライスへ書く。
class FrameRingConstantBuffer {
public:
    /// @brief バッファを確保して常時 Map する
    /// @param dxCommon DirectXCommon（フレームバッファリング数の取得に使う）
    /// @param paramsSize 1 スライスに書く構造体のサイズ（256B 境界へ内部で切り上げる）
    void Initialize(DirectXCommon* dxCommon, uint32_t paramsSize);

    /// @brief 記録中フレームのスライスへ書き込み、その GPU アドレスを返す
    /// @param dxCommon DirectXCommon（記録中フレームインデックスの取得に使う）
    /// @param src 書き込む構造体
    /// @param size 構造体サイズ（Initialize の paramsSize 以下であること）
    /// @return バインドすべき GPU 仮想アドレス。未初期化なら 0
    D3D12_GPU_VIRTUAL_ADDRESS Upload(DirectXCommon* dxCommon, const void* src, uint32_t size);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    uint8_t* mappedBase_ = nullptr;
    uint32_t alignedSize_ = 0;
    uint32_t sliceCount_ = 1;
};

/// @brief レンダリング技術基底クラス
/// @details SSAO、TAA、SSRなどの高度なレンダリング技術の基底クラス
///          PostEffectBase とは異なり、GBuffer・深度・Historyバッファなど
///          複数の入力リソースやフレーム間状態を扱う技術に特化
class RenderingTechniqueBase {
public:
    virtual ~RenderingTechniqueBase() = default;

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    virtual void Initialize(DirectXCommon* dxCommon);

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
    virtual void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// @brief 技術が有効かどうかを取得
    /// @return 有効ならtrue
    bool IsEnabled() const { return enabled_; }

    /// @brief 常時有効な技術かどうかを取得（無効化不可）
    /// @return 常時有効ならtrue
    virtual bool IsAlwaysEnabled() const { return false; }

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
    DirectXCommon* directXCommon_ = nullptr;

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
