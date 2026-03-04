#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>

namespace CoreEngine
{
class DirectXCommon;
class ShaderCompiler;

/// @brief IBLテクスチャ生成クラス
/// @details BRDF LUT、Irradiance Map、Prefiltered Mapを生成
class IBLGenerator
{
public:
    IBLGenerator() = default;
    ~IBLGenerator() = default;

    /// @brief 初期化
    /// @param dxCommon DirectXCommonポインタ
    /// @param shaderCompiler ShaderCompilerポインタ
    void Initialize(DirectXCommon* dxCommon, ShaderCompiler* shaderCompiler);

    /// @brief BRDF LUTを生成
    /// @param size テクスチャサイズ（推奨: 512）
    /// @return 生成されたBRDF LUTテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> GenerateBRDFLUT(uint32_t size = 512);

    /// @brief BRDF LUTをファイルに保存
    /// @param brdfLUT 保存するBRDF LUT
    /// @param outputPath 保存先パス
    /// @return 成功したらtrue
    bool SaveBRDFLUT(ID3D12Resource* brdfLUT, const std::string& outputPath);

    /// @brief BRDF LUTをファイルから読み込み
    /// @param filePath ファイルパス
    /// @return 読み込まれたBRDF LUT（失敗時はnullptr）
    Microsoft::WRL::ComPtr<ID3D12Resource> LoadBRDFLUT(const std::string& filePath);

    /// @brief Irradiance Mapを生成
    /// @param environmentMap 入力環境マップ（キューブマップ）
    /// @param size Irradianceマップのサイズ（推奨: 32）
    /// @return 生成されたIrradiance Map
    Microsoft::WRL::ComPtr<ID3D12Resource> GenerateIrradianceMap(
        ID3D12Resource* environmentMap,
        uint32_t size = 32);

    /// @brief Prefiltered Environment Mapを生成
    /// @param environmentMap 入力環境マップ（キューブマップ）
    /// @param size ベースサイズ（推奨: 128、ミップマップで1/2ずつ縮小）
    /// @return 生成されたPrefiltered Environment Map（5ミップレベル）
    Microsoft::WRL::ComPtr<ID3D12Resource> GeneratePrefilteredEnvironmentMap(
        ID3D12Resource* environmentMap,
        uint32_t size = 128);

private:
    DirectXCommon* dxCommon_ = nullptr;
    ShaderCompiler* shaderCompiler_ = nullptr;

    // BRDF LUT生成用パイプライン
    Microsoft::WRL::ComPtr<ID3D12PipelineState> brdfLutPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> brdfLutRootSignature_;

    // Irradiance Map生成用パイプライン
    Microsoft::WRL::ComPtr<ID3D12PipelineState> irradiancePSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> irradianceRootSignature_;

    // Prefiltered Environment Map生成用パイプライン
    Microsoft::WRL::ComPtr<ID3D12PipelineState> prefilteredPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> prefilteredRootSignature_;

    /// @brief BRDF LUT用のルートシグネチャを作成
    void CreateBRDFLUTRootSignature();

    /// @brief BRDF LUT用のCompute Pipelineを作成
    void CreateBRDFLUTPipeline();

    /// @brief Irradiance Map用のルートシグネチャを作成
    void CreateIrradianceRootSignature();

    /// @brief Irradiance Map用のCompute Pipelineを作成
    void CreateIrradiancePipeline();

    /// @brief Prefiltered Environment Map用のルートシグネチャを作成
    void CreatePrefilteredRootSignature();

    /// @brief Prefiltered Environment Map用のCompute Pipelineを作成
    void CreatePrefilteredPipeline();

    /// @brief UAVキューブマップテクスチャを作成
    /// @param size 各面のサイズ
    /// @param format フォーマット
    /// @return 作成されたキューブマップテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUAVCubemap(
        uint32_t size,
        DXGI_FORMAT format);

    /// @brief UAVキューブマップテクスチャを作成（ミップマップ付き）
    /// @param size 各面のベースサイズ
    /// @param mipLevels ミップレベル数
    /// @param format フォーマット
    /// @return 作成されたキューブマップテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUAVCubemapWithMips(
        uint32_t size,
        uint32_t mipLevels,
        DXGI_FORMAT format);

    /// @brief UAVテクスチャを作成
    /// @param width 幅
    /// @param height 高さ
    /// @param format フォーマット
    /// @return 作成されたテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUAVTexture(
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format);

    /// @brief ディスクリプタヒープを作成
    /// @param type ディスクリプタタイプ
    /// @param numDescriptors ディスクリプタ数
    /// @return 作成されたディスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t numDescriptors);
};
}
