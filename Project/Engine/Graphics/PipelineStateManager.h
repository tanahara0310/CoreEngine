#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <map>
#include <vector>
#include <wrl.h>

using namespace Microsoft::WRL;


namespace CoreEngine
{
enum class BlendMode {
    kBlendModeNone, // ブレンドなし
    kBlendModeNormal, // アルファブレンド
    kBlendModeAdd, // 加算ブレンド
    kBlendModeSubtract, // 減算ブレンド
    kBlendModeMultiply, // 乗算ブレンド
    kBlendModeScreen, // スクリーンブレンド
};

// 前方宣言
class PipelineStateManager;

/// @brief PSOの構築を行うビルダークラス
class PipelineStateBuilder {
public:
    explicit PipelineStateBuilder(PipelineStateManager* manager);

    /// @brief 入力エレメントを追加
    /// @param semanticName セマンティック名
    /// @param semanticIndex インデックス
    /// @param format フォーマット
    /// @param alignedByteOffset アライメントされたバイトオフセット
    /// @param inputSlot 入力スロット(デフォルトは0)
    /// @return ビルダー自身(メソッドチェーン用)
    PipelineStateBuilder& AddInputElement(
        const char* semanticName,
        UINT semanticIndex,
        DXGI_FORMAT format,
        UINT alignedByteOffset,
        UINT inputSlot = 0);

    /// @brief ラスタライザの設定
    /// @param cullMode カリングモード
    /// @param fillMode フィルモード
    /// @return ビルダー自身
    PipelineStateBuilder& SetRasterizer(
        D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK,
        D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID);

    /// @brief 深度バイアスの設定（シャドウアクネ対策）
    /// @param depthBias 深度バイアス値
    /// @param slopeScaledDepthBias スロープスケール深度バイアス
    /// @param depthBiasClamp 深度バイアスクランプ
    /// @return ビルダー自身
    PipelineStateBuilder& SetDepthBias(
        INT depthBias = 0,
        float slopeScaledDepthBias = 0.0f,
        float depthBiasClamp = 0.0f);

    /// @brief 深度ステンシルの設定
    /// @param enableDepth 深度テストの有効化
    /// @param enableWrite 深度書き込みの有効化
    /// @param depthFunc 深度比較関数
    /// @return ビルダー自身
    PipelineStateBuilder& SetDepthStencil(
        bool enableDepth = true,
        bool enableWrite = true,
        D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL);

    /// @brief プリミティブトポロジタイプの設定
    /// @param topologyType トポロジタイプ
    /// @return ビルダー自身
    PipelineStateBuilder& SetPrimitiveTopology(
        D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

    /// @brief レンダーターゲットフォーマットの設定
    /// @param format レンダーターゲットフォーマット
    /// @param index レンダーターゲットのインデックス
    /// @return ビルダー自身
    PipelineStateBuilder& SetRenderTargetFormat(
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        UINT index = 0);

    /// @brief 深度ステンシルフォーマットの設定
    /// @param format 深度ステンシルフォーマット
    /// @return ビルダー自身
    PipelineStateBuilder& SetDepthStencilFormat(
        DXGI_FORMAT format = DXGI_FORMAT_D24_UNORM_S8_UINT);

    /// @brief サンプル数の設定
    /// @param count サンプル数
    /// @param quality サンプル品質
    /// @return ビルダー自身
    PipelineStateBuilder& SetSampleDesc(UINT count = 1, UINT quality = 0);

    /// @brief カラーライトマスクの設定
    /// @param writeMask 書き込みマスク
    /// @param enableAlpha アルファチャンネルの書き込みを有効にするか
    /// @return ビルダー自身
    PipelineStateBuilder& SetColorWriteMask(
        D3D12_COLOR_WRITE_ENABLE writeMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        bool enableAlpha = true);

    /// @brief ブレンドモードを指定してPSOを構築
    /// @param device デバイス
    /// @param vs 頂点シェーダー
    /// @param ps ピクセルシェーダー
    /// @param rootSignature ルートシグネチャ
    /// @param modes 生成するブレンドモード(空の場合はkBlendModeNoneのみ)
    /// @return 構築に成功したか
    bool Build(
        ID3D12Device* device,
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature,
        const std::vector<BlendMode>& modes = {});

    /// @brief 全ブレンドモードでPSOを構築
    /// @param device デバイス
    /// @param vs 頂点シェーダー
    /// @param ps ピクセルシェーダー
    /// @param rootSignature ルートシグネチャ
    /// @return 構築に成功したか
    bool BuildAllBlendModes(
        ID3D12Device* device,
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature);

private:
    friend class PipelineStateManager;

    PipelineStateManager* manager_;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs_;
    D3D12_RASTERIZER_DESC rasterizerDesc_;
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc_;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType_;
    DXGI_FORMAT rtvFormats_[8];
    UINT numRenderTargets_;
    DXGI_FORMAT dsvFormat_;
    DXGI_SAMPLE_DESC sampleDesc_;
    D3D12_COLOR_WRITE_ENABLE colorWriteMask_;
    bool enableAlphaWrite_;
    bool depthWriteEnabled_;

    /// @brief デフォルト値で初期化
    void InitializeDefaults();

    /// @brief ブレンド設定を作成
    D3D12_BLEND_DESC CreateBlendDesc(BlendMode mode) const;

    /// @brief パイプラインステート記述子を作成
    D3D12_GRAPHICS_PIPELINE_STATE_DESC CreatePipelineStateDesc(
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature,
        BlendMode mode) const;
};

/// @brief psoの管理クラス
class PipelineStateManager {
public:
    PipelineStateManager() = default;
    ~PipelineStateManager() = default;

    /// @brief psoの取得
    /// @param mode ブレンドモード
    /// @return パイプラインステート(存在しない場合はnullptr)
    ID3D12PipelineState* GetPipelineState(BlendMode mode = BlendMode::kBlendModeNone);

    /// @brief ビルダーを取得
    /// @return PipelineStateBuilderのインスタンス
    PipelineStateBuilder CreateBuilder();

    /// @brief PSOをクリア
    void Clear();

private:
    friend class PipelineStateBuilder;

    // パイプラインステート
    std::map<BlendMode, ComPtr<ID3D12PipelineState>> pipelineStates_;

    /// @brief PSOを登録
    void RegisterPipelineState(BlendMode mode, ComPtr<ID3D12PipelineState> pso);
};
}
