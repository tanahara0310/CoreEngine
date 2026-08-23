#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"
#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <wrl.h>
#include "Math/MathCore.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

namespace CoreEngine
{
    class AtmosphereManager;
    class DescriptorAllocator;

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
        uint32_t outputHeight;       uint32_t frameIndex;                   // 192
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
        CB_FIELD(VolumetricCloudShaderConstants, frameIndex),
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

    /// @brief 雲の見た目パラメータ（単位はメートル・秒・無次元）
    /// @details 既定値は設計書 12 章の一覧に準拠（地球の積雲を想定）。
    struct VolumetricCloudParameters {
        // ===== 雲層ジオメトリ =====
        float layerBottomAltitudeM = 1500.0f;   ///< 雲底高度 [m]
        float layerThicknessM = 4000.0f;        ///< 層厚 [m]

        // ===== カバレッジ・密度 =====
        /// 全体カバレッジ倍率 [0,1]。高いと空全体が雲デッキで覆われ、視線が雲を長く貫く
        /// ぶん自己遮蔽が累積して「陰影が強い・重い」印象になる。青空の隙間を残す値にする。
        /// 0.48 だとカバレッジ remap（`Remap(baseCloud, 1-coverage, 1, 0, 1)`）が
        /// base shape ノイズの下半分を丸ごと切り捨て、隣接する雲塊をつなぐ「もや」の
        /// 部分が消えて Worley セルの山だけが孤立した粒（ポップコーン状）として残った。
        /// 0.6 前後まで上げ、侵食強度を下げてバランスを取ると、連続したもこもこの
        /// 雲塊になる（後述の detailErosionStrength と合わせて調整すること）。
        float globalCoverage = 0.62f;
        /// 密度→消散係数 [1/m]。密度は coverage（≈0.3〜0.5）で上限クランプされるため、
        /// 実効消散はこの半分以下になる。小さいと縁が 100〜300m のもや状に広がり
        /// 雲が半透明で立体感を失う。実雲のコア相当（0.1 前後）で固い輪郭になる。
        float densityScale = 0.12f;

        // ===== ノイズスケール（変更時は MarkNoiseDirty は不要。サンプルスケールのみ） =====
        /// ベースノイズ 1 タイルの実寸 [m]。雲塊 1 個の幅は概ねこの半分になるため、
        /// 大きすぎると層厚（既定 4km）に対して横長のパンケーキ状の雲になる。
        /// 層厚の 2〜2.5 倍程度が積雲らしいアスペクト比になる。
        /// 小さくしすぎると同じ雲塊の繰り返しが見える（weather map がある程度マスクする）。
        /// 縦方向はこの 0.5 倍でサンプルされる（薄い層に縦方向の変化を持たせるため）。
        float baseNoiseScaleM = 9000.0f;
        float detailNoiseScaleM = 600.0f;       ///< ディテールノイズ 1 タイルの実寸 [m]
        /// 縁の侵食強度 [0,1]。密度は coverage（≈0.5〜0.6）で上限クランプされるため、
        /// これを大きくすると縁だけでなく雲の内部にまで穴が開き、全体が斑点状になる。
        /// coverage より十分小さい値にすること。カリフラワー状の凹凸を出すには
        /// 0.2〜0.25、もや状の柔らかい縁には 0.12〜0.18。
        /// 0.33 は coverage=0.48 との組合せで雲の内部まで侵食し、孤立した粒の集合
        /// （ポップコーン状）になる不具合を起こした。coverage を上げた分、
        /// こちらは下げて「縁だけを荒らす」量に戻す。
        float detailErosionStrength = 0.22f;
        float weatherMapScaleM = 60000.0f;      ///< 天候マップ 1 タイルの実寸 [m]

        // ===== 風（移流アニメーション） =====
        float windDirX = 1.0f;                  ///< 風向 XZ（正規化）
        float windDirZ = 0.0f;
        float windSpeedMPerS = 8.0f;            ///< 風速 [m/s]

        // ===== ライティング =====
        float phaseG0 = 0.8f;                   ///< HG 前方散乱ローブ
        float phaseG1 = -0.3f;                  ///< HG 後方散乱ローブ
        float phaseBlend = 0.5f;                ///< 2 ローブのブレンド [0,1]
        /// Sky-View アンビエント倍率。大きすぎると陰影の暗部が持ち上がり雲が平板に見える。
        /// 小さすぎると太陽光が届かない厚い部分が黒く沈み斑状に見える。
        /// densityScale を上げた（=太陽光の透過が減った）ときは連動して上げること。
        float ambientIntensity = 1.15f;
        /// Powder 効果 [0,1]。照射面の外殻（光学的深さが小さい所）を暗くする効果のため、
        /// 強すぎると順光の雲面全体が一段暗く沈む。0.5 程度が自然。
        float beerPowderStrength = 0.5f;
        float lightMarchStepM = 200.0f;         ///< サンライトマーチ 1 歩 [m]

        // ===== 太陽散乱スケールと多重散乱（Hillaire オクターブ法） =====
        /// 雲の太陽散乱輝度スケール。HG 位相関数の 1/4π 正規化と、sunIntensity が
        /// 放射照度スケールの美術値であることを吸収する係数（UE の雲ライティングスケール相当）。
        /// 大きすぎると明部・暗部ともトーンマッパーの飽和域に入り、雲が真っ白で陰影を失う。
        /// 側方散乱（太陽と視線が 90° 前後）では二重 HG が等方値の半分程度まで落ち、
        /// 順光の雲面が灰色に沈む。3.5 前後で順光の雲が白く見える（太陽方向の
        /// silver lining は明るく飛ぶが、それは実写でも同様）。
        float sunLightScale = 3.5f;
        /// オクターブごとの消散減衰。緩い（0.25〜0.5）と最終オクターブが「深さに依らない
        /// 明るい床」になり、全エネルギーの大半を占めて雲底が一様な白い板になる。
        /// 大きくするほど濃い部分が暗くなり陰影のレンジが広がる。
        float msAttenuation = 0.6f;
        /// オクターブごとの寄与減衰。小さいほど高次オクターブ（=床）の影響が減る。
        /// densityScale が高い（tau が大きい）ほど床は自然に暗くなるため、少し高めでよい。
        float msContribution = 0.5f;
        float msEccentricity = 0.5f;            ///< オクターブごとの位相非対称度の減衰

        // ===== マーチング =====
        float earlyExitTransmittance = 0.005f;  ///< 早期終了しきい値
        /// マーチ最大距離 [m]。短いと雲が地平線のかなり手前で途切れ、地平線際が
        /// 不自然に空っぽになる（実際の空は地平線際ほど雲が密に重なって見える）。
        /// 雲底 1.5km の層が地平線へ沈む距離は約 138km。120km + 遠方フェード +
        /// 空気遠近ヘイズで、雲デッキが視覚上地平線まで連続する。
        float maxMarchDistanceM = 120000.0f;
        /// 反復回数の予算（実際の上限は 2 倍）。ステップ幅は layerThicknessM/128 を基準に
        /// 距離で伸びるため、この値は「予算」であって固定分割数ではない。
        uint32_t maxSteps = 160;

        /// レイマーチの解像度分割数（1=フル解像度, 2=半解像度）。
        /// 実測では 1 にしても見た目の改善は無く（雲底のぼやけは密度勾配由来）、
        /// GPU 使用率が 56% → 86% へ増える。既定は 2。
        uint32_t resolutionDivisor = 2;

        // ===== ゴッドレイ（雲の隙間の光芒） =====
        bool godRayEnabled = true;               ///< ゴッドレイの有効/無効
        float godRayIntensity = 1.0f;            ///< 遮蔽差分（物理項）のスケール。1 が物理値
        float godRayMieBoost = 0.8f;             ///< 加算ミー項（演出）。0 で完全物理
        float godRayMaxDistanceM = 25000.0f;     ///< ビューレイマーチの最大距離 [m]
        uint32_t godRayStepCount = 32;           ///< ビューレイマーチのステップ数
        float cloudShadowRegionSizeM = 60000.0f; ///< 雲シャドウマップのカバー範囲（一辺）[m]
    };

    /// @brief ボリューメトリック雲システムの管理クラス
    /// @details 設計は AtmosphereManager を踏襲する。パラメータ・CB・ノイズテクスチャ・
    ///          レイマーチ/合成パイプラインの保持と、フレーム有効化制御を担当する。
    ///          太陽情報は AtmosphereManager から取得する（単一情報源）。
    class VolumetricCloudManager {
    public:
        // ノイズ解像度（HLSL 側 CS の Dispatch と一致させること）
        static constexpr uint32_t kBaseShapeNoiseSize = 128;
        static constexpr uint32_t kDetailNoiseSize = 32;
        static constexpr uint32_t kWeatherMapSize = 512;
        // 雲シャドウマップ解像度（HLSL 側 GodRayCommon.hlsli の定数と一致させること）
        static constexpr uint32_t kCloudShadowMapSize = 1024;

        /// @brief 初期化
        /// @param device D3D12 デバイス
        /// @param descriptorAllocator ノイズ SRV/UAV 登録先（メインのシェーダー可視ヒープ）
        void Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief フレーム更新（雲を使うシーンの BaseScene::UpdateAtmosphere から呼ばれる）
        /// @details cloudsActive_ を立て、カメラ・時刻・太陽情報を CB へ反映する。
        /// @param atmosphereManager 太陽情報・カメラ高度の取得元（AtmosphereManager と同一の値）
        void Update(const Vector3& cameraWorldPosition,
            const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
            const AtmosphereManager* atmosphereManager,
            float deltaTimeSec);

        // ===== パラメータ =====

        const VolumetricCloudParameters& GetParameters() const { return parameters_; }
        VolumetricCloudParameters& GetParametersMutable() { return parameters_; }

        /// @brief ノイズテクスチャの再生成を要求する（ノイズ形状パラメータ変更時のみ）
        void MarkNoiseDirty() { noiseDirty_ = true; }

        // ===== 機能トグル =====

        /// @brief 雲描画の有効/無効（既定 true。大気シーンで雲だけ OFF にできる）
        void SetEnabled(bool enabled);

        /// @brief エディタ等からパラメータを編集する（CVar へ書き戻して自動保存に載せる）
        void SetParametersFromEditor(const VolumetricCloudParameters& params);
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

        /// @brief ノイズテクスチャと SRV/UAV を生成する
        bool CreateNoiseResources(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief ノイズ生成用コンピュートパイプライン（BaseShape/Detail/Weather）を構築する
        bool CreateNoisePipelines(ID3D12Device* device);

        /// @brief レイマーチ・合成用コンピュートパイプラインを構築する（Phase 2 で実装）
        bool CreateRenderPipelines(ID3D12Device* device);

        /// @brief 半解像度 CloudBuffer と合成用中間テクスチャを SceneColor サイズ追従で確保する（Phase 2 で実装）
        bool EnsureCloudTargets(GpuResource& sceneColor);

        /// @brief 雲シャドウマップテクスチャと CB を生成する（ゴッドレイ用）
        bool CreateGodRayResources(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief ゴッドレイ用コンピュートパイプライン群を構築する
        bool CreateGodRayPipelines(ID3D12Device* device);

        /// @brief ゴッドレイ CB を現在のカメラ・パラメータで更新する
        void UploadGodRayConstants(const AtmosphereManager* atmosphereManager);

        // ===== シェーダープロバイダ（AtmosphereManager の Provider 構造体群を踏襲） =====
        struct BaseShapeNoiseShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudBaseShapeNoise.CS.hlsl"; }
        };
        /// @brief ディテールノイズ生成 CS のシェーダ供給
        struct DetailNoiseShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudDetailNoise.CS.hlsl"; }
        };
        /// @brief 天候マップ生成 CS のシェーダ供給
        struct WeatherMapShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudWeatherMap.CS.hlsl"; }
        };
        /// @brief 雲本体のレイマーチ CS のシェーダ供給
        struct RayMarchShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudRayMarch.CS.hlsl"; }
        };
        /// @brief 雲を SceneColor へ合成する CS のシェーダ供給
        struct CompositeShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudComposite.CS.hlsl"; }
        };
        /// @brief 空キューブマップへの雲焼き込み CS のシェーダ供給
        struct CloudCubemapCaptureShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudCubemapCapture.CS.hlsl"; }
        };
        /// @brief 雲シャドウマップ生成 CS のシェーダ供給
        struct CloudShadowMapShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"CloudShadowMap.CS.hlsl"; }
        };
        /// @brief ゴッドレイのレイマーチ CS のシェーダ供給
        struct GodRayMarchShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"GodRayMarch.CS.hlsl"; }
        };
        /// @brief ゴッドレイ合成 CS のシェーダ供給
        struct GodRayCompositeShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"GodRayComposite.CS.hlsl"; }
        };

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
        float distanceFromPlanetCenter_ = 6360000.0f;
        float timeSec_ = 0.0f;
        uint32_t frameIndex_ = 0;

        // 状態フラグ
        bool enabled_ = true;           ///< 機能トグル（既定 true）
        bool cloudsActive_ = false;     ///< このフレームで Update() が呼ばれ雲が要求されたか
        bool noiseDirty_ = true;        ///< ノイズ再生成が必要か
        bool noiseGenerated_ = false;   ///< ノイズ生成済みか
        bool noisePipelinesReady_ = false; ///< ノイズ生成パイプライン構築済みか
        bool pipelinesReady_ = false;   ///< レイマーチ/合成パイプライン構築済みか（Phase 2）

        ID3D12Device* device_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        // GPU リソース
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        VolumetricCloudShaderConstants* constantData_ = nullptr; // 永続マップ先

        // ノイズテクスチャ（Phase 1 で生成）
        GpuResource baseShapeNoise_;
        DescriptorHandle baseShapeNoiseSrvHandle_{};
        DescriptorHandle baseShapeNoiseUavHandle_{};

        GpuResource detailNoise_;
        DescriptorHandle detailNoiseSrvHandle_{};
        DescriptorHandle detailNoiseUavHandle_{};

        GpuResource weatherMap_;
        DescriptorHandle weatherMapSrvHandle_{};
        DescriptorHandle weatherMapUavHandle_{};

        // 半解像度レイマーチ結果（Phase 2 で確保）
        GpuResource cloudBuffer_;
        DescriptorHandle cloudBufferSrvHandle_{};
        DescriptorHandle cloudBufferUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE cloudBufferSrvCpuHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE cloudBufferUavCpuHandle_{};

        // 合成用中間テクスチャ（SceneColor と同サイズ・Phase 2 で確保）
        GpuResource compositeResult_;
        DescriptorHandle compositeResultUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE compositeResultUavCpuHandle_{};
        uint64_t targetsWidth_ = 0;
        uint32_t targetsHeight_ = 0;

        // ===== ゴッドレイ =====
        // 雲シャドウマップ（1024² R16_FLOAT。太陽方向の雲透過率の上面図）
        GpuResource cloudShadowMap_;
        DescriptorHandle cloudShadowMapSrvHandle_{};
        DescriptorHandle cloudShadowMapUavHandle_{};

        // 半解像度ゴッドレイバッファ（EnsureCloudTargets で cloudBuffer_ と同サイズ確保）
        GpuResource godRayBuffer_;
        DescriptorHandle godRayBufferSrvHandle_{};
        DescriptorHandle godRayBufferUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE godRayBufferSrvCpuHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE godRayBufferUavCpuHandle_{};

        // ゴッドレイ定数バッファ（永続マップ）
        Microsoft::WRL::ComPtr<ID3D12Resource> godRayConstantBuffer_;
        GodRayShaderConstants* godRayConstantData_ = nullptr;
        bool godRayPipelinesReady_ = false;

        // パイプライン
        CustomShaderPipeline baseShapeNoisePipeline_{};
        BaseShapeNoiseShaderProvider baseShapeNoiseShaderProvider_{};
        CustomShaderPipeline detailNoisePipeline_{};
        DetailNoiseShaderProvider detailNoiseShaderProvider_{};
        CustomShaderPipeline weatherMapPipeline_{};
        WeatherMapShaderProvider weatherMapShaderProvider_{};
        CustomShaderPipeline rayMarchPipeline_{};
        RayMarchShaderProvider rayMarchShaderProvider_{};
        CustomShaderPipeline compositePipeline_{};
        CompositeShaderProvider compositeShaderProvider_{};
        CustomShaderPipeline cubemapCapturePipeline_{};
        CloudCubemapCaptureShaderProvider cubemapCaptureShaderProvider_{};
        CustomShaderPipeline cloudShadowPipeline_{};
        CloudShadowMapShaderProvider cloudShadowShaderProvider_{};
        CustomShaderPipeline godRayMarchPipeline_{};
        GodRayMarchShaderProvider godRayMarchShaderProvider_{};
        CustomShaderPipeline godRayCompositePipeline_{};
        GodRayCompositeShaderProvider godRayCompositeShaderProvider_{};
    };
}
