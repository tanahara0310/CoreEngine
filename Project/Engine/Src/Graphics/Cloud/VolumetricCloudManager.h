#pragma once

#include "Graphics/Cloud/CloudPipelines.h"
#include "Graphics/Cloud/CloudResources.h"
#include "Graphics/Cloud/CloudSettings.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include <d3d12.h>
#include <wrl.h>
#include "Math/MathCore.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"

namespace CoreEngine
{
    class AtmosphereManager;
    class DescriptorAllocator;
    class GraphicsCore;

    /// @brief 雲シェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 CloudCommon.hlsli の CloudConstants と一致させること（256 バイト）。
    ///          距離はメートル基準。sunDirection は「光の進行方向」（大気散乱と同じ規約）。
    struct VolumetricCloudShaderConstants {
        Matrix4x4 invViewProj;                                              // 0
        Vector3 cameraWorldPos;      float timeSec;                         // 64
        Vector3 sunDirection;        float sunIntensity;                    // 80
        Vector3 sunColor;            float planetRadiusM;                   // 96
        float layerBottomAltitudeM;  float layerThicknessM;
        float groundLevelY;          float globalCoverage;                  // 112
        float baseNoiseScaleM;       float detailNoiseScaleM;
        float detailErosionStrength; float densityScale;                   // 128
        float windDirX;              float windDirZ;
        float windSpeedMPerS;        float weatherMapScaleM;               // 144
        float phaseG0;               float phaseG1;
        float phaseBlend;            float ambientIntensity;                // 160
        float beerPowderStrength;    float lightMarchStepM;
        float earlyExitTransmittance; float maxMarchDistanceM;             // 176
        uint32_t maxSteps;           uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t pad0;                         // 192
        float sunLightScale;         float msAttenuation;
        float msContribution;        float msEccentricity;                  // 208
        // ===== 月（第2大気ライト。夜の雲の直接照明） =====
        Vector3 moonDirection;       float moonIntensity;                   // 224
        Vector3 moonColor;           float hasMoon;                         // 240 (= 256)
    };
    static_assert(sizeof(VolumetricCloudShaderConstants) == 256,
        "VolumetricCloudShaderConstants は HLSL 側 CloudConstants の 256 バイトレイアウトと一致させること");

    static constexpr Cb::Field kVolumetricCloudShaderConstantsFields[] = {
        CB_FIELD(VolumetricCloudShaderConstants, invViewProj),
        CB_FIELD(VolumetricCloudShaderConstants, cameraWorldPos),
        CB_FIELD(VolumetricCloudShaderConstants, timeSec), CB_FIELD(VolumetricCloudShaderConstants, sunDirection),
        CB_FIELD(VolumetricCloudShaderConstants, sunIntensity), CB_FIELD(VolumetricCloudShaderConstants, sunColor),
        CB_FIELD(VolumetricCloudShaderConstants, planetRadiusM),
        CB_FIELD(VolumetricCloudShaderConstants, layerBottomAltitudeM),
        CB_FIELD(VolumetricCloudShaderConstants, layerThicknessM),
        CB_FIELD(VolumetricCloudShaderConstants, groundLevelY),
        CB_FIELD(VolumetricCloudShaderConstants, globalCoverage),
        CB_FIELD(VolumetricCloudShaderConstants, baseNoiseScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, detailNoiseScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, detailErosionStrength),
        CB_FIELD(VolumetricCloudShaderConstants, densityScale), CB_FIELD(VolumetricCloudShaderConstants, windDirX),
        CB_FIELD(VolumetricCloudShaderConstants, windDirZ),
        CB_FIELD(VolumetricCloudShaderConstants, windSpeedMPerS),
        CB_FIELD(VolumetricCloudShaderConstants, weatherMapScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, phaseG0), CB_FIELD(VolumetricCloudShaderConstants, phaseG1),
        CB_FIELD(VolumetricCloudShaderConstants, phaseBlend),
        CB_FIELD(VolumetricCloudShaderConstants, ambientIntensity),
        CB_FIELD(VolumetricCloudShaderConstants, beerPowderStrength),
        CB_FIELD(VolumetricCloudShaderConstants, lightMarchStepM),
        CB_FIELD(VolumetricCloudShaderConstants, earlyExitTransmittance),
        CB_FIELD(VolumetricCloudShaderConstants, maxMarchDistanceM),
        CB_FIELD(VolumetricCloudShaderConstants, maxSteps), CB_FIELD(VolumetricCloudShaderConstants, outputWidth),
        CB_FIELD(VolumetricCloudShaderConstants, outputHeight),
        CB_FIELD(VolumetricCloudShaderConstants, pad0),
        CB_FIELD(VolumetricCloudShaderConstants, sunLightScale),
        CB_FIELD(VolumetricCloudShaderConstants, msAttenuation),
        CB_FIELD(VolumetricCloudShaderConstants, msContribution),
        CB_FIELD(VolumetricCloudShaderConstants, msEccentricity),
        CB_FIELD(VolumetricCloudShaderConstants, moonDirection),
        CB_FIELD(VolumetricCloudShaderConstants, moonIntensity),
        CB_FIELD(VolumetricCloudShaderConstants, moonColor), CB_FIELD(VolumetricCloudShaderConstants, hasMoon),
    };
    CB_VERIFY_LAYOUT(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields);
    CB_BIND_HLSL(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields, "gCloud");

    /// @brief ゴッドレイシェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 GodRayCommon.hlsli の GodRayConstants と一致させること（128 バイト）。
    ///          太陽方向・散乱係数などは gAtmosphere / gCloud 側 CB から取るため持たない。
    struct GodRayShaderConstants {
        Matrix4x4 invViewProj;                                      // 0
        Vector3 cameraWorldPos;      float maxDistanceM;            // 64
        float shadowRegionCenterX;   float shadowRegionCenterZ;
        float shadowRegionSizeM;     float shadowAnchorWorldY;      // 80
        float intensity;             float mieBoost;
        float groundLevelY;          float edgeFadeStart;           // 96
        uint32_t stepCount;          uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t pad0;                 // 112 (= 128)
    };
    static_assert(sizeof(GodRayShaderConstants) == 128,
        "GodRayShaderConstants は HLSL 側 GodRayConstants の 128 バイトレイアウトと一致させること");

    static constexpr Cb::Field kGodRayShaderConstantsFields[] = {
        CB_FIELD(GodRayShaderConstants, invViewProj), CB_FIELD(GodRayShaderConstants, cameraWorldPos),
        CB_FIELD(GodRayShaderConstants, maxDistanceM), CB_FIELD(GodRayShaderConstants, shadowRegionCenterX),
        CB_FIELD(GodRayShaderConstants, shadowRegionCenterZ), CB_FIELD(GodRayShaderConstants, shadowRegionSizeM),
        CB_FIELD(GodRayShaderConstants, shadowAnchorWorldY), CB_FIELD(GodRayShaderConstants, intensity),
        CB_FIELD(GodRayShaderConstants, mieBoost), CB_FIELD(GodRayShaderConstants, groundLevelY),
        CB_FIELD(GodRayShaderConstants, edgeFadeStart), CB_FIELD(GodRayShaderConstants, stepCount),
        CB_FIELD(GodRayShaderConstants, outputWidth), CB_FIELD(GodRayShaderConstants, outputHeight),
        CB_FIELD(GodRayShaderConstants, pad0),
    };
    CB_VERIFY_LAYOUT(GodRayShaderConstants, kGodRayShaderConstantsFields);
    CB_BIND_HLSL(GodRayShaderConstants, kGodRayShaderConstantsFields, "gGodRay");

    /// @brief ボリューメトリック雲システムの管理クラス
    /// @details 設計は AtmosphereManager を踏襲する。パラメータ・CB・ノイズテクスチャ・
    ///          レイマーチ/合成パイプラインの保持と、フレーム有効化制御を担当する。
    ///          太陽情報は AtmosphereManager から取得する（単一情報源）。
    class VolumetricCloudManager {
    public:
        /// @brief 初期化
        /// @param graphicsCore デバイスと、ターゲット再確保前の GPU 完了待ちの取得元
        /// @param descriptorAllocator ノイズ SRV/UAV 登録先（メインのシェーダー可視ヒープ）
        void Initialize(GraphicsCore* graphicsCore, DescriptorAllocator* descriptorAllocator);

        /// @brief フレーム更新（雲を使うシーンの BaseScene::UpdateAtmosphere から呼ばれる）
        /// @details cloudsActive_ を立て、カメラ・時刻・太陽情報を CB へ反映する。
        /// @param atmosphereManager 太陽情報・カメラ高度の取得元（AtmosphereManager と同一の値）
        void Update(const Vector3& cameraWorldPosition,
            const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
            const AtmosphereManager* atmosphereManager,
            float deltaTimeSec);

        // ===== パラメータ =====

        /// @brief 現在のパラメータ（実体は CloudCVars。Update が毎フレーム取り込む）
        /// @note 変更するときは CloudCVars 側を Set すること（ここへ書いても次の Update で戻る）
        const VolumetricCloudParameters& GetParameters() const { return parameters_; }

        // ===== 機能トグル =====

        /// @brief 雲描画の有効/無効（既定 true。大気シーンで雲だけ OFF にできる）
        void SetEnabled(bool enabled);
        bool IsEnabled() const { return enabled_; }

        // ===== フレーム有効化（AtmosphereManager と同一パターン） =====

        /// @brief このフレームで雲が要求されているか（Update() が呼ばれ、かつ enabled_）
        bool AreCloudsActive() const { return cloudsActive_ && enabled_; }

        /// @brief フレーム終端で有効化フラグをリセットする（EngineSystem が全 View 描画後に呼ぶ）
        void ResetFrameActivation() { cloudsActive_ = false; }

        // ===== ノイズ生成（VolumetricCloudNoisePass から毎フレーム呼ばれる） =====

        /// @brief ダーティ時のみノイズテクスチャ群を再生成する
        /// @param cmdList 記録先コマンドリスト
        void GenerateNoiseTexturesIfNeeded(ID3D12GraphicsCommandList* cmdList);

        /// @brief ノイズテクスチャが生成済みか
        bool AreNoiseTexturesReady() const { return noiseGenerated_; }

        // ===== 雲描画（VolumetricCloudPass から呼ばれる） =====

        /// @brief 雲をレイマーチして SceneColor へ合成する
        /// @param sceneColor SceneColor（実体＋現在ステート）
        void RenderClouds(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& sceneColor,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
            const AtmosphereManager* atmosphereManager);

        // ===== 空キューブマップへの雲焼き込み（AtmosphereLUTPass から呼ばれる） =====

        /// @brief 空キューブマップへ雲を前乗算合成する（スペキュラ IBL / 水面の雲反射用）
        /// @warning CaptureSkyEnvironment の直後・PrefilterSkyEnvironment の前に呼ぶこと
        ///          （キューブマップは UAV 状態が前提）
        void RenderCloudsToSkyCubemap(
            ID3D12GraphicsCommandList* cmdList,
            const AtmosphereManager* atmosphereManager);

        // ===== ゴッドレイ（GodRayPass から呼ばれる） =====

        /// @brief 雲シャドウマップ生成 → ゴッドレイマーチ → SceneColor 合成
        /// @details 合成モデルは差分法（遮蔽あり − 遮蔽なし ≤ 0 を加算）。
        ///          既存の Sky-View / Aerial Perspective が加算済みの「遮蔽なし内散乱」との
        ///          二重加算を避けつつ、雲影の空気柱を暗くして光芒の明暗対比を作る。
        void RenderGodRays(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& sceneColor,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
            const AtmosphereManager* atmosphereManager);

    private:
        /// @brief 現在のパラメータ・カメラ・太陽情報から定数バッファを更新する
        void UploadConstants();

        /// @brief ゴッドレイ CB を現在のカメラ・パラメータで更新する
        void UploadGodRayConstants();

        /// @brief フレームターゲットを現在の SceneColor サイズと分割数で確保する
        bool EnsureFrameTargets(GpuResource& sceneColor);

        /// @brief 合成中間テクスチャの実サイズで合成 CS をディスパッチする
        void DispatchComposite(ID3D12GraphicsCommandList* cmdList) const;

        VolumetricCloudParameters parameters_{};

        // フレーム更新で計算される値
        Vector3 cameraWorldPos_{};
        Matrix4x4 invViewProj_{};
        Vector3 sunDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector3 sunColor_ = { 1.0f, 1.0f, 1.0f };
        float sunIntensity_ = 1.0f;
        Vector3 moonDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector3 moonColor_ = { 1.0f, 1.0f, 1.0f };
        float moonIntensity_ = 0.0f;
        bool hasMoon_ = false;
        /// 雲層シェルの基準。大気と同じ値を使う（AtmosphereParameters の既定で初期化）
        float planetRadiusM_ = 6360000.0f;
        float groundLevelY_ = 0.0f;
        float timeSec_ = 0.0f;

        // 状態フラグ
        bool enabled_ = true;           ///< 機能トグル（既定 true）
        bool cloudsActive_ = false;     ///< このフレームで Update() が呼ばれ雲が要求されたか
        bool noiseDirty_ = true;        ///< ノイズ再生成が必要か
        bool noiseGenerated_ = false;   ///< ノイズ生成済みか
        bool noisePipelinesReady_ = false;  ///< ノイズ生成パイプライン構築済みか
        bool pipelinesReady_ = false;       ///< レイマーチ/合成パイプライン構築済みか
        bool godRayPipelinesReady_ = false; ///< ゴッドレイパイプライン構築済みか

        GraphicsCore* graphicsCore_ = nullptr;
        ID3D12Device* device_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        // 定数バッファ（永続マップ）
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        VolumetricCloudShaderConstants* constantData_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> godRayConstantBuffer_;
        GodRayShaderConstants* godRayConstantData_ = nullptr;

        CloudResources resources_{};
        CloudPipelines pipelines_{};
    };
}
