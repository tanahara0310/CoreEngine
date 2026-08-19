#pragma once

#include <d3d12.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Light.h"
#include "LightData.h"
#include "LightBufferManager.h"
#include "LightDebugVisualizer.h"

namespace CoreEngine
{
    class ResourceFactory;
    class DescriptorManager;

    /// @brief ライトマネージャー（ライトの管理と制御を担当）
    /// @details オーサリングは統一 Light 構造体＋世代付き LightHandle で行い、
    ///          UpdateAll() が GPU 転送用の型別構造体へ変換する。
    /// @note ディレクショナルライトの転送順は大気の太陽を先頭にした正準順
    ///       （RT シャドウマスクとシェーダー側配列の対応を保証するため）。
    class LightManager {
    public:
        static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
        static constexpr uint32_t MAX_POINT_LIGHTS = 16;
        static constexpr uint32_t MAX_SPOT_LIGHTS = 16;
        static constexpr uint32_t MAX_AREA_LIGHTS = 8;
        static constexpr uint32_t MAX_TOTAL_LIGHTS =
            MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS + MAX_AREA_LIGHTS;

    public:
        /// @brief 初期化
        /// @param device D3D12デバイス
        /// @param resourceFactory リソースファクトリ
        /// @param descriptorManager ディスクリプタマネージャー
        void Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager);

        /// @brief 全てのライトを更新（オーサリング表現 → GPU バッファへの変換・転送）
        void UpdateAll();

        /// @brief ライトのImGuiを描画（Inspector: 選択ライトのプロパティ or 概要）
        void DrawAllImGui();

        /// @brief Hierarchy の Lighting 配下に各ライトの子行を描画する
        /// @return 子行がクリックされた場合 true（Inspector を Lighting へルーティングする）
        bool DrawLightTreeImGui();

        /// @brief ライト編集 UI の選択を解除する（Lighting 親エントリ選択時＝概要表示へ戻す）
        void ClearLightUISelection();

        // ==================== ライトの生成・破棄・参照 ====================

        /// @brief ライトを生成する
        /// @param type ライトの種類
        /// @param name エディタ表示名（空なら "Point 1" のように自動命名）
        /// @return 生成されたライトのハンドル（種類ごとの最大数を超えた場合は無効ハンドル）
        LightHandle CreateLight(LightType type, std::string name = {});

        /// @brief ライトを破棄する
        /// @return 破棄できた場合 true（無効ハンドル・破棄済みの場合 false）
        bool DestroyLight(LightHandle handle);

        /// @brief ハンドルからライトを取得する
        /// @return ライトへのポインタ（破棄済み・無効ハンドルの場合は nullptr）。
        ///         ポインタの保持は不可。ハンドルを保持し、フレームごとに引き直すこと。
        Light* GetLight(LightHandle handle);
        /// @brief ハンドルからライトを引く（世代が古ければ nullptr）
        const Light* GetLight(LightHandle handle) const;

        /// @brief 名前でライトを検索する（同名がある場合は最初の1つ）
        LightHandle FindLightByName(std::string_view name) const;

        /// @brief 全ライトを列挙する（生成順）
        void ForEachLight(const std::function<void(LightHandle, Light&)>& fn);

        /// @brief 指定種類のライト数を取得する
        uint32_t GetLightCount(LightType type) const;

        /// @brief 指定種類の最大ライト数を取得する
        static uint32_t GetMaxLightCount(LightType type);

        // ==================== ディレクショナルライトの正準順アクセス ====================

        /// @brief ディレクショナルライトを正準順（GPU 転送順。0 = メイン/太陽）で取得
        /// @return ライトへのポインタ（範囲外の場合はnullptr）
        Light* GetDirectionalLight(size_t index);

        /// @brief 大気散乱の太陽として使用するディレクショナルライトを取得
        /// @return isAtmosphereSun が立っている最初のライト。
        ///         無ければ最初のディレクショナルライトへフォールバック、それも無ければ nullptr
        Light* GetAtmosphereSunLight();

        /// @brief 大気散乱の月（第2大気ライト）として使用するディレクショナルライトを取得
        /// @return isAtmosphereMoon が立っている最初のライト。太陽と違いフォールバックは無い
        ///         （月はオプトイン。無ければ nullptr = 月なし）
        Light* GetAtmosphereMoonLight();

        // ==================== 大気透過率（Transmittance on Light） ====================

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
        ///          大気の太陽・月でないライトはそのままの色を返す。
        Vector3 GetEffectiveLightColorRGB(const Light& light) const;

        // ==================== その他 ====================

        /// @brief 全てのライトをクリア（シーン切り替え時に使用）
        void ClearAllLights();

        /// @brief エリアライトの法線から発光面の正規直交基底を導出する
        /// @details GPU 転送と可視化で同じ基底を使うための共通ヘルパー
        static void ComputeAreaLightBasis(const Vector3& normal, Vector3& outRight, Vector3& outUp);

        // ==================== GPU バインディング ====================

        /// @brief コマンドリストにライトをセット
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

    private:
        /// @brief ライト1個分のスロット（インデックス安定・世代でダングリング検出）
        struct Slot {
            Light light;
            uint16_t generation = 0;
            bool alive = false;
        };

        /// @brief GetAtmosphereSunLight と同じ選択規則の const 版（透過率の適用先判定に共用）
        const Light* FindAtmosphereSunLight() const;

        /// @brief GetAtmosphereMoonLight と同じ選択規則の const 版
        const Light* FindAtmosphereMoonLight() const;

        /// @brief ディレクショナルライトの正準順スロットインデックスを収集する
        /// @details 生成順を基本に、大気の太陽（無ければ先頭）を先頭へ移動した順。
        ///          GPU 転送・GetDirectionalLight・RT シャドウのインデックスがこの順で一致する。
        std::vector<uint16_t> CollectDirectionalSlotsCanonical() const;

        /// @brief 種類ごとの既定値（物理単位）を設定する
        static void SetupDefaults(Light& light, LightType type);

        std::vector<Slot> slots_;

        /// @brief 太陽ライトの GPU 転送色へ乗算する大気透過率（大気非アクティブ時は {1,1,1}）
        Vector3 atmosphereSunTransmittance_ = { 1.0f, 1.0f, 1.0f };

        /// @brief 月ライトの GPU 転送色へ乗算する大気透過率（月なし・大気非アクティブ時は {1,1,1}）
        Vector3 atmosphereMoonTransmittance_ = { 1.0f, 1.0f, 1.0f };

        LightBufferManager bufferManager_;
        LightDebugVisualizer debugVisualizer_;
    };
}
