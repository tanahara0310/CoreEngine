#pragma once

#include <memory>
#include <vector>
#include <d3d12.h>

#include "LightData.h"
#include "LightBufferManager.h"
#include "LightDebugVisualizer.h"

namespace CoreEngine
{
    class ResourceFactory;
    class DescriptorManager;

    /// @brief ライトマネージャー（ライトの管理と制御を担当）
    class LightManager {
    public:
        static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
        static constexpr uint32_t MAX_POINT_LIGHTS = 16;
        static constexpr uint32_t MAX_SPOT_LIGHTS = 16;
        static constexpr uint32_t MAX_AREA_LIGHTS = 8;

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

        /// @brief エリアライトを追加
        /// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
        AreaLightData* AddAreaLight();

        /// @brief コマンドリストにライトをセット
        /// @param commandList コマンドリスト
        /// @param lightCountsRootParameterIndex ライトカウント用のルートパラメータインデックス
        /// @param directionalLightsRootParameterIndex ディレクショナルライト用のルートパラメータインデックス
        /// @param pointLightsRootParameterIndex ポイントライト用のルートパラメータインデックス
        /// @param spotLightsRootParameterIndex スポットライト用のルートパラメータインデックス
        /// @param areaLightsRootParameterIndex エリアライト用のルートパラメータインデックス
        void SetLightsToCommandList(
            ID3D12GraphicsCommandList* commandList,
            int lightCountsRootParameterIndex,
            int directionalLightsRootParameterIndex,
            int pointLightsRootParameterIndex,
            int spotLightsRootParameterIndex,
            int areaLightsRootParameterIndex
        );

        /// @brief ライトカウントバッファのGPU仮想アドレスを取得
        D3D12_GPU_VIRTUAL_ADDRESS GetLightCountsGPUAddress() const;

        /// @brief ディレクショナルライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const;

        /// @brief ポイントライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const;

        /// @brief スポットライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const;

        /// @brief エリアライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetAreaLightsSRVHandle() const;

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

        /// @brief エリアライトの有効/無効を設定
        /// @param index ライトのインデックス
        /// @param enabled 有効にする場合true
        void SetAreaLightEnabled(size_t index, bool enabled);

        /// @brief ディレクショナルライトを取得
        /// @param index ライトのインデックス
        /// @return ライトデータへのポインタ（範囲外の場合はnullptr）
        DirectionalLightData* GetDirectionalLight(size_t index);

        /// @brief 大気散乱の太陽として使用するディレクショナルライトを取得
        /// @return isAtmosphereSun が立っている最初のライト。
        ///         無ければメインライト（インデックス0）へフォールバック、それも無ければ nullptr
        DirectionalLightData* GetAtmosphereSunLight();

        /// @brief 大気散乱の月（第2大気ライト）として使用するディレクショナルライトを取得
        /// @return isAtmosphereMoon が立っている最初のライト。太陽と違いフォールバックは無い
        ///         （月はオプトイン。無ければ nullptr = 月なし）
        DirectionalLightData* GetAtmosphereMoonLight();

        /// @brief 太陽ライトへ適用する大気透過率を設定する（AtmosphereManager が毎フレーム呼ぶ）
        /// @details authored なライトデータは変更せず、GPU 転送時のコピーの color へ乗算される
        ///          （UE の Transmittance on light color 相当）。大気非アクティブ時は {1,1,1}。
        void SetAtmosphereSunTransmittance(const Vector3& transmittance) {
            atmosphereSunTransmittance_ = transmittance;
        }

        /// @brief 太陽ライトへ適用中の大気透過率を取得する
        const Vector3& GetAtmosphereSunTransmittance() const { return atmosphereSunTransmittance_; }

        /// @brief 月ライトへ適用する大気透過率を設定する（太陽版と同じ配管。月の出入りの減光・赤方偏移）
        void SetAtmosphereMoonTransmittance(const Vector3& transmittance) {
            atmosphereMoonTransmittance_ = transmittance;
        }

        /// @brief 月ライトへ適用中の大気透過率を取得する
        const Vector3& GetAtmosphereMoonTransmittance() const { return atmosphereMoonTransmittance_; }

        /// @brief 大気透過率を適用した実効色（RGB）を取得する
        /// @details GPU 転送値と同じ色。CPU 側でライト色を直接参照する箇所
        ///          （水面コースティクス等）はこれを使うことで日没の減光・赤方偏移に追従する。
        ///          大気の太陽でないライトはそのままの色を返す。
        Vector3 GetEffectiveLightColorRGB(const DirectionalLightData& light) const;

        /// @brief 全てのライトをクリア（シーン切り替え時に使用）
        void ClearAllLights();

        /// @brief メインディレクショナルライトのビュープロジェクション行列を計算
        /// @param sceneCenter シーンの中心座標
        /// @param sceneRadius シーンを囲む半径
        /// @return ライトビュープロジェクション行列
        Matrix4x4 CalculateMainDirectionalLightViewProjection(const Vector3& sceneCenter, float sceneRadius);

    private:
        /// @brief GetAtmosphereSunLight と同じ選択規則の const 版（透過率の適用先判定に共用）
        const DirectionalLightData* FindAtmosphereSunLight() const;

        /// @brief GetAtmosphereMoonLight と同じ選択規則の const 版
        const DirectionalLightData* FindAtmosphereMoonLight() const;

        std::vector<DirectionalLightData> directionalLights_;
        std::vector<PointLightData> pointLights_;
        std::vector<SpotLightData> spotLights_;
        std::vector<AreaLightData> areaLights_;

        /// @brief 太陽ライトの GPU 転送色へ乗算する大気透過率（大気非アクティブ時は {1,1,1}）
        Vector3 atmosphereSunTransmittance_ = { 1.0f, 1.0f, 1.0f };

        /// @brief 月ライトの GPU 転送色へ乗算する大気透過率（月なし・大気非アクティブ時は {1,1,1}）
        Vector3 atmosphereMoonTransmittance_ = { 1.0f, 1.0f, 1.0f };

        LightBufferManager bufferManager_;
        LightDebugVisualizer debugVisualizer_;
    };
}
