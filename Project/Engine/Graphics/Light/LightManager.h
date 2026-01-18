#pragma once

#include <memory>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

#include "LightData.h"
#include "MathCore.h"

namespace CoreEngine
{
// 前方宣言
class ResourceFactory;
class DescriptorManager;

/// @brief ライトマネージャー
class LightManager {
public:
    /// @brief 各ライトタイプの最大数
    static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
    static constexpr uint32_t MAX_POINT_LIGHTS = 16;
    static constexpr uint32_t MAX_SPOT_LIGHTS = 16;

public:
    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param resourceFactory リソースファクトリ
    /// @param descriptorManager ディスクリプタマネージャー
    void Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager);

    /// @brief 全てのライトを更新
    void UpdateAll();

    /// @brief ライトのImGuiを描画
    void DrawAllImGui();

    /// @brief ディレクショナルライトを追加
    /// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
    DirectionalLightData* AddDirectionalLight();

    /// @brief ポイントライトを追加
    /// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
    PointLightData* AddPointLight();

    /// @brief スポットライトを追加
    /// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
    SpotLightData* AddSpotLight();

    /// @brief GPU用のライトバッファを更新
    void UpdateLightBuffers();

    /// @brief コマンドリストにライトをセット
    /// @param commandList コマンドリスト
    /// @param lightCountsRootParameterIndex ライトカウント用のルートパラメータインデックス
    /// @param directionalLightsRootParameterIndex ディレクショナルライト用のルートパラメータインデックス
    /// @param pointLightsRootParameterIndex ポイントライト用のルートパラメータインデックス
    /// @param spotLightsRootParameterIndex スポットライト用のルートパラメータインデックス
    void SetLightsToCommandList(
        ID3D12GraphicsCommandList* commandList,
        UINT lightCountsRootParameterIndex,
        UINT directionalLightsRootParameterIndex,
        UINT pointLightsRootParameterIndex,
        UINT spotLightsRootParameterIndex
    );

    /// @brief ライトカウントバッファのGPU仮想アドレスを取得
    D3D12_GPU_VIRTUAL_ADDRESS GetLightCountsGPUAddress() const;

    /// @brief ディレクショナルライトSRVのGPUハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const { return directionalLightsSRVHandle_; }

    /// @brief ポイントライトSRVのGPUハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const { return pointLightsSRVHandle_; }

    /// @brief スポットライトSRVのGPUハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const { return spotLightsSRVHandle_; }

    /// @brief ディレクショナルライトの有効/無効を設定
    /// @param index ライトのインデックス
    /// @param enabled 有効にする場合true
    void SetDirectionalLightEnabled(size_t index, bool enabled);

    /// @brief ポイントライトの有効/無効を設定
    /// @param index ライトのインデックス
    /// @param enabled 有効にする場合true
    void SetPointLightEnabled(size_t index, bool enabled);

    /// @brief スポットライトの有効/無効を設定
    /// @param index ライトのインデックス
    /// @param enabled 有効にする場合true
    void SetSpotLightEnabled(size_t index, bool enabled);

    /// @brief 全てのライトをクリア（シーン切り替え時に使用）
    void ClearAllLights();

private:
    /// @brief StructuredBuffer用のリソースを作成
    void CreateStructuredBufferResources(ID3D12Device* device);

    /// @brief StructuredBuffer用のSRVを作成
    void CreateStructuredBufferSRVs(DescriptorManager* descriptorManager);

private:
    // CPU側のライトデータ配列
    std::vector<DirectionalLightData> directionalLights_;
    std::vector<PointLightData> pointLights_;
    std::vector<SpotLightData> spotLights_;

    // GPU側のStructuredBufferリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightsBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightsBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightsBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightCountsBuffer_;

    // StructuredBufferのSRV用GPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE directionalLightsSRVHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE pointLightsSRVHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE spotLightsSRVHandle_{};

    // マップされたライトカウントデータ
    LightCounts* lightCountsData_ = nullptr;

    // デバイスとリソースファクトリの保持
    ID3D12Device* device_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;
};
}
