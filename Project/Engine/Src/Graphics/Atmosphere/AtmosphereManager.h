#pragma once

#include <d3d12.h>
#include <wrl.h>
#include "Math/MathCore.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

namespace CoreEngine
{
    class LightManager;
    class DescriptorManager;

    /// @brief 大気散乱シェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 AtmosphereCommon.hlsli の AtmosphereConstants と一致させること。
    ///          float32 精度確保のため距離は km、係数は 1/km に変換して格納する。
    struct AtmosphereShaderConstants {
        Vector3 sunDirection;         float sunIntensity;
        Vector3 sunColor;             float planetRadiusKm;
        Vector3 rayleighScattering;   float rayleighScaleHeightKm; // [1/km], [km]
        Vector3 ozoneAbsorption;      float atmosphereTopRadiusKm; // [1/km], [km]
        float mieScattering;          float mieAbsorption;         // [1/km]
        float mieScaleHeightKm;       float miePhaseG;
        float ozoneLayerCenterKm;     float ozoneLayerHalfWidthKm;
        float cameraRadiusKm;         float sunDiskHalfAngleRad;   // 太陽の視半径 [rad]
        Vector3 groundAlbedo;         float sunDiskLuminanceScale; // ディスク輝度スケール
        Matrix4x4 invViewProj;        // 逆ビュープロジェクション行列（Aerial Perspective 用）
        Vector3 cameraWorldPos;       float groundLevelY;          // カメラのワールド座標 [m] / 地表とみなす世界Y座標 [m]
    };
    static_assert(sizeof(AtmosphereShaderConstants) == 192,
        "AtmosphereShaderConstants は HLSL 側 AtmosphereConstants の 192 バイトレイアウトと一致させること");

    /// @brief 大気散乱の物理パラメータ
    /// @details 単位はメートル基準（散乱・吸収係数は 1/m）。
    ///          既定値は Hillaire 2020 "A Scalable and Production Ready Sky and
    ///          Atmosphere Rendering Technique"（UE SkyAtmosphere の元論文）に基づく地球大気。
    struct AtmosphereParameters {
        // ===== ジオメトリ =====
        float planetRadius = 6360000.0f;        ///< 惑星（地表）半径 [m]
        float atmosphereTopRadius = 6460000.0f; ///< 大気圏上端の惑星中心からの半径 [m]

        // ===== レイリー散乱（空気分子。青空の成分） =====
        Vector3 rayleighScattering = { 5.802e-6f, 13.558e-6f, 33.1e-6f }; ///< 散乱係数 [1/m]
        float rayleighScaleHeight = 8000.0f;    ///< 密度が1/eになる高度スケール [m]

        // ===== ミー散乱（エアロゾル。太陽周りのハロや霞の成分） =====
        float mieScattering = 3.996e-6f;        ///< 散乱係数 [1/m]（波長非依存）
        float mieAbsorption = 4.4e-6f;          ///< 吸収係数 [1/m]
        float mieScaleHeight = 1200.0f;         ///< 密度スケールハイト [m]
        float miePhaseG = 0.8f;                 ///< Henyey-Greenstein 位相関数の非対称度 [-1,1]

        // ===== オゾン吸収（散乱せず吸収のみ。夕暮れの色再現に寄与） =====
        Vector3 ozoneAbsorption = { 0.650e-6f, 1.881e-6f, 0.085e-6f }; ///< 吸収係数 [1/m]
        float ozoneLayerCenter = 25000.0f;      ///< オゾン層中心高度 [m]（テント型分布）
        float ozoneLayerHalfWidth = 15000.0f;   ///< オゾン層の半幅 [m]

        // ===== 地表 =====
        Vector3 groundAlbedo = { 0.3f, 0.3f, 0.3f }; ///< 地表アルベド（多重散乱の地面反射寄与）
        float groundLevelY = 0.0f;              ///< 地表とみなすワールドY座標 [m]（シーン側で指定）

        // ===== 太陽ディスク =====
        float sunDiskAngularRadiusDeg = 0.265f; ///< 太陽の視半径 [deg]（実際の太陽は約0.265°）
        float sunDiskLuminanceScale = 50.0f;    ///< ディスク輝度スケール（空の散乱輝度に対する倍率）
    };

    /// @brief 大気散乱システムの管理クラス
    /// @details パラメータ・太陽情報・カメラ高度の保持と、LUT 生成リソースの管理を担当する。
    ///          ワールド座標は 1unit = 1m 前提。ワールド全体を惑星スケールへ変換せず、
    ///          カメラの Y 座標のみを groundLevelY 基準の高度として惑星中心距離へ変換する。
    class AtmosphereManager {
    public:
        // LUT 解像度（HLSL 側 AtmosphereCommon.hlsli の定数と一致させること）
        static constexpr uint32_t kTransmittanceLUTWidth = 256;
        static constexpr uint32_t kTransmittanceLUTHeight = 64;
        static constexpr uint32_t kMultiScatteringLUTSize = 32;
        static constexpr uint32_t kSkyViewLUTWidth = 192;
        static constexpr uint32_t kSkyViewLUTHeight = 108;
        static constexpr uint32_t kCameraVolumeSize = 32;

        /// @brief 初期化
        /// @param device D3D12デバイス
        /// @param descriptorManager LUT の SRV/UAV 登録先（メインのシェーダー可視ヒープ）
        void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager);

        /// @brief フレーム更新
        /// @param cameraWorldPosition カメラのワールド座標 [m]
        /// @param viewMatrix カメラのビュー行列（Aerial Perspective 用）
        /// @param projMatrix カメラのプロジェクション行列（Aerial Perspective 用）
        /// @param lightManager 太陽情報の取得元（GetAtmosphereSunLight() を使用）
        void Update(const Vector3& cameraWorldPosition,
            const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
            LightManager* lightManager);

        // ===== パラメータ =====

        /// @brief パラメータを取得（読み取り専用）
        const AtmosphereParameters& GetParameters() const { return parameters_; }

        /// @brief パラメータを適用（LUT 再計算のダーティフラグを立てる）
        void SetParameters(const AtmosphereParameters& params) {
            parameters_ = params;
            MarkLUTDirty();
        }

        /// @brief 大気パラメータ由来の LUT 再計算を要求する
        /// @details Transmittance / Multi-Scattering / Sky-View の全 LUT を再生成する。
        ///          太陽方向・カメラ高度の変化は Update() が自動検知するため呼び出し不要。
        void MarkLUTDirty() { paramsDirty_ = true; }

        /// @brief LUT の再計算が必要か（パラメータ or 太陽/カメラ変化）
        bool IsLUTDirty() const { return paramsDirty_ || skyViewDirty_; }

        // ===== フレーム更新結果 =====

        /// @brief 太陽光の進行方向（正規化済み。太陽の位置方向はこの逆ベクトル）
        const Vector3& GetSunDirection() const { return sunDirection_; }

        /// @brief 太陽光の色
        const Vector4& GetSunColor() const { return sunColor_; }

        /// @brief 太陽光の強度
        float GetSunIntensity() const { return sunIntensity_; }

        /// @brief 有効な太陽ライトが取得できているか
        bool HasSunLight() const { return hasSunLight_; }

        /// @brief カメラの地表からの高度 [m]
        float GetCameraHeightAboveGround() const { return cameraHeightAboveGround_; }

        /// @brief カメラの惑星中心からの距離 [m]
        float GetDistanceFromPlanetCenter() const { return distanceFromPlanetCenter_; }

        /// @brief パラメータへの直接アクセス（変更した場合は MarkLUTDirty() を呼ぶこと）
        AtmosphereParameters& GetParametersMutable() { return parameters_; }

        /// @brief 大気散乱定数バッファの GPU 仮想アドレスを取得（ルート CBV バインド用）
        D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferGPUAddress() const {
            return constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
        }

        /// @brief 定数バッファが利用可能か
        bool IsConstantBufferReady() const { return constantData_ != nullptr; }

        // ===== LUT =====

        /// @brief ダーティフラグが立っていれば LUT 群を再生成する（AtmosphereLUTPass から毎フレーム呼ばれる）
        /// @param cmdList 記録先コマンドリスト
        void GenerateLUTsIfNeeded(ID3D12GraphicsCommandList* cmdList);

        /// @brief Transmittance LUT の SRV ハンドルを取得（描画シェーダーのバインド用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTransmittanceLUTSRVHandle() const { return transmittanceSrvHandle_; }

        /// @brief Multi-Scattering LUT の SRV ハンドルを取得（描画シェーダーのバインド用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetMultiScatteringLUTSRVHandle() const { return multiScatteringSrvHandle_; }

        /// @brief Sky-View LUT の SRV ハンドルを取得（描画シェーダーのバインド用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSkyViewLUTSRVHandle() const { return skyViewSrvHandle_; }

        // ===== Aerial Perspective =====

        /// @brief SceneColor へ空気遠近感を合成する（AerialPerspectivePass から呼ばれる）
        /// @param cmdList 記録先コマンドリスト
        /// @param sceneColor SceneColor リソース
        /// @param sceneColorState SceneColor の現在状態（追跡参照）
        /// @param sceneColorSrvHandle SceneColor の SRV ハンドル
        /// @param depthSrvHandle SceneDepth の SRV ハンドル
        void ApplyAerialPerspective(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* sceneColor,
            D3D12_RESOURCE_STATES& sceneColorState,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle);

        /// @brief LUT 生成パイプラインが利用可能か
        bool AreLUTsReady() const { return lutsGenerated_; }

        // ===== フレーム有効化 =====

        /// @brief このフレームで大気散乱が要求されているか
        /// @details Update() を呼ぶシーン（大気を使うシーン）でのみ true になる。
        ///          AtmosphereLUTPass / AerialPerspectivePass はこれが false のとき
        ///          SceneColor に触れずスキップする（大気非対応シーンへの漏れ出し防止）。
        bool IsAtmosphereActive() const { return atmosphereActive_; }

        /// @brief フレーム終端で有効化フラグをリセットする（EngineSystem が全 View 描画後に呼ぶ）
        void ResetFrameActivation() { atmosphereActive_ = false; }

    private:
        /// @brief 現在のパラメータ・太陽情報から定数バッファを更新する
        void UploadConstants();

        /// @brief LUT テクスチャと SRV/UAV を生成する
        bool CreateLUTResources(ID3D12Device* device, DescriptorManager* descriptorManager);

        /// @brief Transmittance / Multi-Scattering LUT を再生成する（パラメータ変更時のみ）
        void GenerateBaseLUTs(ID3D12GraphicsCommandList* cmdList);

        /// @brief Sky-View LUT を再生成する（太陽方向・カメラ高度変更時）
        void GenerateSkyViewLUT(ID3D12GraphicsCommandList* cmdList);

        /// @brief LUT 生成用コンピュートパイプラインを構築する
        bool CreateLUTPipelines(ID3D12Device* device);

        /// @brief Transmittance LUT 用シェーダープロバイダ
        struct TransmittanceLUTShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"TransmittanceLUT.CS.hlsl"; }
        };

        /// @brief Multi-Scattering LUT 用シェーダープロバイダ
        struct MultiScatteringLUTShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"MultiScatteringLUT.CS.hlsl"; }
        };

        /// @brief Sky-View LUT 用シェーダープロバイダ
        struct SkyViewLUTShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"SkyViewLUT.CS.hlsl"; }
        };

        /// @brief Camera Volume LUT 用シェーダープロバイダ
        struct CameraVolumeLUTShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CameraVolumeLUT.CS.hlsl"; }
        };

        /// @brief Aerial Perspective 合成用シェーダープロバイダ
        struct AerialPerspectiveShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"AerialPerspective.CS.hlsl"; }
        };

        /// @brief Camera Volume LUT を再生成する（カメラ・太陽・パラメータ変更時）
        void GenerateCameraVolumeLUT(ID3D12GraphicsCommandList* cmdList);

        /// @brief Aerial Perspective 合成用の中間テクスチャを SceneColor と同サイズで確保する
        bool EnsureAerialPerspectiveTarget(ID3D12Resource* sceneColor);
        AtmosphereParameters parameters_{};

        // フレーム更新で計算される値
        Vector3 sunDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector4 sunColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        float sunIntensity_ = 1.0f;
        bool hasSunLight_ = false;
        float cameraHeightAboveGround_ = 0.0f;
        float distanceFromPlanetCenter_ = 6360000.0f;

        bool paramsDirty_ = true;   ///< 大気パラメータ変更 → 全 LUT 再生成
        bool skyViewDirty_ = true;  ///< 太陽方向・カメラ高度変更 → Sky-View LUT のみ再生成
        bool lutsGenerated_ = false;
        bool atmosphereActive_ = false; ///< このフレームで Update() が呼ばれ大気が要求されたか
        ID3D12Device* device_ = nullptr;

        // Sky-View の変化検知用キャッシュ
        Vector3 lastSunDirection_ = { 0.0f, -1.0f, 0.0f };
        float lastCameraRadius_ = 0.0f;

        // Camera Volume の変化検知用キャッシュ
        bool cameraVolumeDirty_ = true;
        Matrix4x4 lastInvViewProj_{};
        Matrix4x4 invViewProj_{};
        Vector3 cameraWorldPos_{};

        // GPU リソース
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        AtmosphereShaderConstants* constantData_ = nullptr; // 永続マップ先

        // Transmittance LUT
        Microsoft::WRL::ComPtr<ID3D12Resource> transmittanceLUT_;
        D3D12_RESOURCE_STATES transmittanceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE transmittanceSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE transmittanceUavHandle_{};

        // Multi-Scattering LUT
        Microsoft::WRL::ComPtr<ID3D12Resource> multiScatteringLUT_;
        D3D12_RESOURCE_STATES multiScatteringState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE multiScatteringSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE multiScatteringUavHandle_{};

        // Sky-View LUT
        Microsoft::WRL::ComPtr<ID3D12Resource> skyViewLUT_;
        D3D12_RESOURCE_STATES skyViewState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE skyViewSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE skyViewUavHandle_{};

        // Camera Volume LUT（froxel 3D テクスチャ）
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraVolumeLUT_;
        D3D12_RESOURCE_STATES cameraVolumeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeUavHandle_{};

        // Aerial Perspective 合成用中間テクスチャ（SceneColor と同サイズ）
        Microsoft::WRL::ComPtr<ID3D12Resource> apResult_;
        D3D12_RESOURCE_STATES apResultState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE apResultUavHandle_{};
        uint64_t apResultWidth_ = 0;
        uint32_t apResultHeight_ = 0;
        DescriptorManager* descriptorManager_ = nullptr;

        // LUT 生成パイプライン
        CustomShaderPipeline transmittancePipeline_{};
        TransmittanceLUTShaderProvider transmittanceShaderProvider_{};
        CustomShaderPipeline multiScatteringPipeline_{};
        MultiScatteringLUTShaderProvider multiScatteringShaderProvider_{};
        CustomShaderPipeline skyViewPipeline_{};
        SkyViewLUTShaderProvider skyViewShaderProvider_{};
        CustomShaderPipeline cameraVolumePipeline_{};
        CameraVolumeLUTShaderProvider cameraVolumeShaderProvider_{};
        CustomShaderPipeline aerialPerspectivePipeline_{};
        AerialPerspectiveShaderProvider aerialPerspectiveShaderProvider_{};
        bool pipelinesReady_ = false;
    };
}
