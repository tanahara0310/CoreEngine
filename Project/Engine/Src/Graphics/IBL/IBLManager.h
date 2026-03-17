#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>

namespace CoreEngine
{
    class DirectXCommon;
    class IBLGenerator;

    /// @brief IBLシステム管理クラス
    /// @details Irradiance Map、Prefiltered Map、BRDF LUTの生成と管理を一元化
    class IBLManager
    {
    public:
        struct IBLSRVHandles
        {
            D3D12_GPU_DESCRIPTOR_HANDLE irradiance = {};
            D3D12_GPU_DESCRIPTOR_HANDLE prefiltered = {};
            D3D12_GPU_DESCRIPTOR_HANDLE brdfLUT = {};
        };

        // ===== デフォルトサイズ定数 =====
        static constexpr uint32_t DEFAULT_IRRADIANCE_SIZE = 128;
        static constexpr uint32_t DEFAULT_PREFILTERED_SIZE = 256;
        static constexpr uint32_t DEFAULT_BRDF_LUT_SIZE = 512;
        static constexpr uint32_t PREFILTERED_MIP_LEVELS = 5;

        /// @brief 初期化パラメータ
        struct InitParams
        {
            ID3D12Resource* environmentMap = nullptr;              ///< 環境マップ
            uint32_t irradianceSize = DEFAULT_IRRADIANCE_SIZE;     ///< Irradianceマップサイズ（推奨: 128）
            uint32_t prefilteredSize = DEFAULT_PREFILTERED_SIZE;   ///< Prefilteredマップサイズ（推奨: 256）
            uint32_t brdfLUTSize = DEFAULT_BRDF_LUT_SIZE;          ///< BRDF LUTサイズ（推奨: 512）
        };

        IBLManager() = default;
        ~IBLManager() = default;

        /// @brief IBLシステムを初期化
        /// @param dxCommon DirectXCommonポインタ
        /// @param iblGenerator IBLGeneratorポインタ
        /// @param params 初期化パラメータ
        /// @return 成功したらtrue
        bool Initialize(DirectXCommon* dxCommon, IBLGenerator* iblGenerator, const InitParams& params);

        /// @brief Irradiance Mapを取得
        /// @return Irradiance Mapリソース
        ID3D12Resource* GetIrradianceMap() const { return irradianceMap_.Get(); }

        /// @brief Prefiltered Mapを取得
        /// @return Prefiltered Mapリソース
        ID3D12Resource* GetPrefilteredMap() const { return prefilteredMap_.Get(); }

        /// @brief BRDF LUTを取得
        /// @return BRDF LUTリソース
        ID3D12Resource* GetBRDFLUT() const { return brdfLUT_.Get(); }

        /// @brief 初期化済みかどうかを取得
        /// @return 初期化済みならtrue
        bool IsInitialized() const { return isInitialized_; }

        /// @brief IBL SRVハンドルを取得
        /// @return Irradiance / Prefiltered / BRDF LUT のSRVハンドル
        IBLSRVHandles GetSRVHandles() const { return { irradianceSRV_, prefilteredSRV_, brdfLUTSRV_ }; }

    private:
        DirectXCommon* dxCommon_ = nullptr;

        // IBLリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> irradianceMap_;
        Microsoft::WRL::ComPtr<ID3D12Resource> prefilteredMap_;
        Microsoft::WRL::ComPtr<ID3D12Resource> brdfLUT_;

        // SRVハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceSRV_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSRV_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTSRV_ = {};

        bool isInitialized_ = false;

        /// @brief Irradiance MapのSRVを作成
        /// @return 成功したらtrue
        bool CreateIrradianceSRV();

        /// @brief Prefiltered MapのSRVを作成
        /// @return 成功したらtrue
        bool CreatePrefilteredSRV();

        /// @brief BRDF LUTのSRVを作成
        /// @return 成功したらtrue
        bool CreateBRDFLUTSRV();
    };

} // namespace CoreEngine
