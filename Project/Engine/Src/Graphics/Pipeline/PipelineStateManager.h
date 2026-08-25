#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <array>
#include <vector>
#include <string>
#include <wrl.h>

using namespace Microsoft::WRL;


namespace CoreEngine
{
// 前方宣言
class ShaderReflectionData;

enum class BlendMode {
    kBlendModeNone, // ブレンドなし
    kBlendModeNormal, // アルファブレンド
    kBlendModeAdd, // 加算ブレンド
    kBlendModeSubtract, // 減算ブレンド
    kBlendModeMultiply, // 乗算ブレンド
    kBlendModeScreen, // スクリーンブレンド
};

/// @brief BlendMode の要素数（PipelineStateManager の固定長配列用）
inline constexpr size_t kBlendModeCount = 6;

// 前方宣言
class PipelineStateManager;

/// @brief PSOの構築を行うビルダークラス
class PipelineStateBuilder {
public:
    /// @brief 生成した PSO の登録先マネージャを指定して構築する
    explicit PipelineStateBuilder(PipelineStateManager* manager);

    // inputElementDescs_ の SemanticName が semanticNameStorage_ 内の文字列を
    // 指しているため、コピーすると新しい方がコピー元の文字列を指したままになり
    // ダングリングする（過去に実際に発生したバグ）。コピーは型レベルで禁止する。
    // move は vector のバッファ所有権移動で要素アドレスが変わらないため安全。
    PipelineStateBuilder(const PipelineStateBuilder&) = delete;
    PipelineStateBuilder& operator=(const PipelineStateBuilder&) = delete;
    PipelineStateBuilder(PipelineStateBuilder&&) = default;
    PipelineStateBuilder& operator=(PipelineStateBuilder&&) = default;

    /// @brief デバッグ名の設定
    /// @note PSO の SetName と、生成失敗時のエラーログに使用される。
    ///       PIX やデバッグレイヤーのメッセージで PSO を識別できるようになる。
    /// @param name デバッグ名（例: "SkyBox", "ModelForward"）
    /// @return ビルダー自身(メソッドチェーン用)
    PipelineStateBuilder& SetDebugName(const std::string& name);

    /// @brief シェーダーリフレクションから入力レイアウトを自動設定
    /// @param reflectionData シェーダーリフレクションデータ
    /// @return ビルダー自身(メソッドチェーン用)
    PipelineStateBuilder& SetInputLayoutFromReflection(const ShaderReflectionData& reflectionData);

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

    /// @brief レンダーターゲットフォーマットの設定（単一RT用）
    /// @note G-Bufferなど複数RTを設定する場合は SetRenderTargetFormats() を使用してください。
    /// @param format レンダーターゲットフォーマット
    /// @param index レンダーターゲットのインデックス
    /// @return ビルダー自身
    PipelineStateBuilder& SetRenderTargetFormat(
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        UINT index = 0);

    /// @brief [MRT] 複数レンダーターゲットのフォーマットを一括設定（G-Buffer 向け）
    /// @note 単一 RT なら SetRenderTargetFormat() で足りる。count は最大 8
    PipelineStateBuilder& SetRenderTargetFormats(const DXGI_FORMAT* formats, UINT count);

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

    /// @brief ブレンドモードを指定して PSO を構築
    /// @param modes 生成するブレンドモード（空なら kBlendModeNone のみ）
    bool Build(
        ID3D12Device* device,
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature,
        const std::vector<BlendMode>& modes = {});

    /// @brief 全ブレンドモードで PSO を構築（単一 RT・フォワードパス向け）
    /// @note G-Buffer パスは BuildGBuffer() を使うこと。MRT で呼ぶと警告を出す
    bool BuildAllBlendModes(
        ID3D12Device* device,
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature);

    /// @brief G-Buffer 専用 PSO 構築（kBlendModeNone のみ・全 RT スロットのブレンド設定済み）
    /// @note G-Buffer は透過が不要なので BuildAllBlendModes() は使わないこと
    bool BuildGBuffer(
        ID3D12Device* device,
        IDxcBlob* vs,
        IDxcBlob* ps,
        ID3D12RootSignature* rootSignature);

private:
friend class PipelineStateManager;

PipelineStateManager* manager_;
std::string debugName_;  // SetName・エラーログ用のデバッグ名
std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs_;
std::vector<std::string> semanticNameStorage_;  // セマンティック名の永続化用
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
    /// @note 要求されたモードが未生成の場合は kBlendModeNone へフォールバックする
    ///       （モードごとに初回のみ警告ログを出す）。kBlendModeNone も無い場合は nullptr。
    /// @param mode ブレンドモード
    /// @return パイプラインステート
    ID3D12PipelineState* GetPipelineState(BlendMode mode = BlendMode::kBlendModeNone);

    /// @brief ビルダーを取得
    /// @return PipelineStateBuilderのインスタンス
    PipelineStateBuilder CreateBuilder();

    /// @brief PSOをクリア
    void Clear();

private:
    friend class PipelineStateBuilder;

    // パイプラインステート（BlendMode を添字にした固定長配列。未生成スロットは nullptr）
    // GetPipelineState は毎ドロー呼ばれるため、map の探索ではなく配列添字にしている
    std::array<ComPtr<ID3D12PipelineState>, kBlendModeCount> pipelineStates_;

    // 未生成モードのフォールバック警告を出したモードのビットマスク（ログスパム防止）
    uint32_t warnedMissingModes_ = 0;

    /// @brief PSOを登録
    void RegisterPipelineState(BlendMode mode, ComPtr<ID3D12PipelineState> pso);
};
}
