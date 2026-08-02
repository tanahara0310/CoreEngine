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
        float multiScatteringFactor;  float apKmPerSlice;          // 多重散乱スケール / APの1スライスあたり距離 [km]
        float constantsPad[2];        // パディング
        // ===== 月（第2大気ライト。UE の Second Atmosphere Light 相当） =====
        Vector3 moonDirection;        float moonIntensity;         // 月光の進行方向 / 強度（sunIntensity と同ドメイン）
        Vector3 moonColor;            float moonDiskHalfAngleRad;  // 月光色 / 月の視半径 [rad]
        float moonDiskLuminanceScale; float hasMoon;               // 月ディスク輝度スケール / 月有効フラグ (0/1)
        float moonPhaseFromSun;       float starIntensity;         // 満ち欠けの太陽連動 (0=常に満月) / 星空の強度 (0=無効)
    };
    static_assert(sizeof(AtmosphereShaderConstants) == 256,
        "AtmosphereShaderConstants は HLSL 側 AtmosphereConstants の 256 バイトレイアウトと一致させること");

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

        // ===== 月ディスク =====
        float moonDiskAngularRadiusDeg = 0.259f; ///< 月の視半径 [deg]（実際の月の平均視半径）
        /// @brief 月ディスクの輝度スケール（moonIntensity に乗算）
        /// @details 既定 25 は moonIntensity=0.02 でディスク輝度 ≈ 0.3（昼の空 ≈ 1 の約1/3）になる値。
        ///          実際の満月面輝度（約2500 cd/m² ≒ 昼空の1/3）に合わせた美術値で、
        ///          夜空では白い円盤・昼の空にはほぼ埋もれる現実の見え方になる。
        float moonDiskLuminanceScale = 25.0f;

        /// @brief 月の満ち欠けを太陽方向から計算するか
        /// @details false（既定）= 常に満月。ゲームでは太陽と月を独立に動かすことが多く、
        ///          実位相だと配置次第で意図せず新月（ほぼ見えない月）になるため固定満月を既定とする。
        ///          true = 太陽・月の位置関係から実際の位相（新月〜満月）を自動計算する。
        bool moonPhaseFromSun = false;

        // ===== 星空（Phase 5: 手続き的星field） =====
        /// @brief 星空の強度スケール（0=無効）
        /// @details シェーダー内で方向ハッシュから生成する星の輝度倍率。テクスチャ不要。
        ///          空の輝度による暗さマスクが掛かるため、昼・薄明・月グレア近傍では
        ///          有効のままでも自動的に見えなくなる（常時 ON で害がない）。
        float starIntensity = 1.0f;

        // ===== 多重散乱 =====
        /// @brief 多重散乱寄与のスケール（1=近似そのまま）
        /// @details Hillaire の等方 Psi_ms 近似は薄明時（太陽が地平線際）に空全体、
        ///          特に反太陽側を過大に持ち上げる傾向がある。UE の MultiScatteringFactor 相当。
        float multiScatteringFactor = 1.0f;

        // ===== Aerial Perspective =====
        /// @brief Camera Volume LUT の1スライスあたり距離 [km]（既定 4 = 最大 32×4 = 128km）
        /// @details UE の Aerial Perspective View Distance Scale 相当。大きくすると霞が
        ///          より遠距離まで届く代わりに近距離の分解能が落ちる。
        float apKmPerSlice = 4.0f;
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
        static constexpr uint32_t kSkyIrradianceSHCoeffCount = 9; ///< 2次SH係数の数
        static constexpr uint32_t kSkyCubemapSize = 64;           ///< 空キューブマップ 1 面の解像度
        static constexpr uint32_t kSkySpecularMipCount = 5;       ///< プリフィルタ済みスペキュラのミップ数（64→4）

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
        /// @note groundLevelY 以外の実体は CVar（"r.Atmosphere.*"）が保持する。
        ///       外部から編集する場合は SetParametersFromEditor を使うこと（CVar へ書き戻される）
        void SetParameters(const AtmosphereParameters& params) {
            parameters_ = params;
            MarkLUTDirty();
        }

        /// @brief エディタ等からパラメータを編集する（CVar へ書き戻して自動保存に載せる）
        /// @param params 適用するパラメータ
        void SetParametersFromEditor(const AtmosphereParameters& params);

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

        /// @brief 地表から太陽方向への大気透過率（惑星遮蔽込み。Update() で毎フレーム計算）
        /// @details 昼はほぼ白、夕方は赤方偏移、日没後はゼロへ滑らかに減衰する。
        ///          太陽ディレクショナルライトの実効色の変調（UE の Transmittance on light color 相当）に使う。
        const Vector3& GetSunTransmittance() const { return sunTransmittance_; }

        // ===== 月（第2大気ライト） =====

        /// @brief 月光の進行方向（正規化。月の位置方向はこの逆ベクトル）
        const Vector3& GetMoonDirection() const { return moonDirection_; }

        /// @brief 月光の色
        const Vector4& GetMoonColor() const { return moonColor_; }

        /// @brief 月光の強度（大気の輝度ドメイン）
        float GetMoonIntensity() const { return moonIntensity_; }

        /// @brief 有効な月ライトが取得できているか
        bool HasMoonLight() const { return hasMoonLight_; }

        /// @brief 地表から月方向への大気透過率（惑星遮蔽込み。月ライト有効時のみ計算）
        const Vector3& GetMoonTransmittance() const { return moonTransmittance_; }

        /// @brief 照明駆動露出用のシーン代表輝度（カメラ非依存。Update() で毎フレーム計算）
        /// @details 太陽・月の高度と強度から「屋外の代表的な画面平均輝度」を解析的に見積もった値。
        ///          ToneMapping の照明駆動測光モードがこれをターゲット輝度として使うことで、
        ///          カメラの向き（画面の構図）に露出が影響されなくなる。
        float GetSceneIlluminationLuminance() const { return sceneIlluminationLuminance_; }

        /// @brief 太陽直接光への透過率適用（Transmittance on Light）の有効/無効
        /// @details 無効時は LightManager へ {1,1,1} を渡す（＝従来動作）。既定は有効。
        void SetTransmittanceOnLightEnabled(bool enabled);
        bool IsTransmittanceOnLightEnabled() const { return transmittanceOnLight_; }

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

        /// @brief CameraVolume（Aerial Perspective froxel）LUT の SRV ハンドルを取得
        /// @details 半透明パス（水面等）が自前で空気遠近感を適用する際に使う。
        ///          生成後は PIXEL_SHADER_RESOURCE 込みの状態のためピクセルシェーダーから直接サンプル可能
        D3D12_GPU_DESCRIPTOR_HANDLE GetCameraVolumeLUTSRVHandle() const { return cameraVolumeSrvHandle_; }

        // ===== 空アンビエント（Sky Light 相当） =====

        /// @brief 空の放射照度 SH9 係数バッファの SRV ハンドルを取得（DeferredLighting 用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSkyIrradianceSHSRVHandle() const { return skyIrradianceSrvHandle_; }

        /// @brief 空アンビエントの SH バッファが一度でも生成されたか
        /// @details false の間は内容が未定義のため、DeferredLighting 側で無効扱いにすること
        bool IsSkyAmbientReady() const { return skyIrradianceGenerated_; }

        /// @brief 空アンビエント（大気からの環境光）の有効/無効
        void SetSkyAmbientEnabled(bool enabled);
        bool IsSkyAmbientEnabled() const { return skyAmbientEnabled_; }

        /// @brief 空アンビエントの強度スケール（空の輝度単位 → サーフェス光単位の変換係数）
        void SetSkyAmbientScale(float scale);
        float GetSkyAmbientScale() const { return skyAmbientScale_; }

        // ===== 空スペキュラIBL（Phase 3b: Sky Light のスペキュラ成分相当） =====

        /// @brief 空スペキュラIBL（空＋雲キューブマップの環境反射）の有効/無効
        void SetSkySpecularEnabled(bool enabled);
        bool IsSkySpecularEnabled() const { return skySpecularEnabled_; }

        /// @brief プリフィルタ済み空スペキュラキューブマップの SRV ハンドルを取得
        /// @details TextureCube・kSkySpecularMipCount ミップ。mip = roughness×(ミップ数-1) で評価する。
        ///          α には雲の透過率が入っている（水面が平面反射との合成不透明度に使う）。
        D3D12_GPU_DESCRIPTOR_HANDLE GetSkySpecularSRVHandle() const { return skySpecularSrvHandle_; }

        /// @brief 空キューブマップ（mip0・雲合成前後の作業面）の UAV ハンドルを取得
        /// @details VolumetricCloudManager が雲を前乗算合成する際のバインド先。
        D3D12_GPU_DESCRIPTOR_HANDLE GetSkyCubemapUAVHandle() const { return skyCubemapUavHandle_; }

        /// @brief 空スペキュラキューブマップが一度でも生成されたか
        bool IsSkyEnvironmentReady() const { return skyEnvironmentGenerated_; }

        /// @brief このフレームで空キューブマップの再生成が必要か（消費型）
        /// @param cloudsActive 雲が有効なフレームか（雲は毎フレーム動くため常時再生成になる）
        /// @param frameNumber フレーム通し番号（View 間の二重実行ガード）
        /// @details true を返した場合、呼び出し側は CaptureSkyEnvironment →
        ///          （雲があれば）雲合成 → PrefilterSkyEnvironment の順で実行すること。
        bool ConsumeSkyEnvironmentDirty(bool cloudsActive, uint64_t frameNumber);

        /// @brief Sky-View LUT から空キューブマップ（mip0）を再生成する
        void CaptureSkyEnvironment(ID3D12GraphicsCommandList* cmdList);

        /// @brief 空キューブマップを GGX プリフィルタしてスペキュラミップ群を生成する
        void PrefilterSkyEnvironment(ID3D12GraphicsCommandList* cmdList);

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

        /// @brief 照明駆動露出用のシーン代表輝度を計算する（太陽・月の高度・強度から解析）
        float ComputeSceneIlluminationLuminance() const;

        /// @brief 地表から指定ライト方向への大気透過率を計算する（CPU レイマーチ 40 ステップ）
        /// @details HLSL 側 AtmosphereCommon.hlsli の ComputeExtinction / PlanetSunVisibility の忠実な移植。
        ///          評価点は地表（groundLevelY 相当）+2m。シーンのオブジェクトを照らす光の減衰なので
        ///          カメラ高度ではなく地表基準で評価する。太陽・月で共用する。
        /// @param lightDirection ライトの進行方向（正規化済み。光源の位置方向はこの逆）
        Vector3 ComputeLightTransmittanceCPU(const Vector3& lightDirection) const;

        /// @brief LUT テクスチャと SRV/UAV を生成する
        bool CreateLUTResources(ID3D12Device* device, DescriptorManager* descriptorManager);

        /// @brief Transmittance / Multi-Scattering LUT を再生成する（パラメータ変更時のみ）
        void GenerateBaseLUTs(ID3D12GraphicsCommandList* cmdList);

        /// @brief Sky-View LUT を再生成する（太陽方向・カメラ高度変更時）
        void GenerateSkyViewLUT(ID3D12GraphicsCommandList* cmdList);

        /// @brief Sky-View LUT から空の放射照度 SH9 係数を再生成する（Sky-View 再生成時のみ）
        void GenerateSkyIrradianceSH(ID3D12GraphicsCommandList* cmdList);

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

        /// @brief 空アンビエント SH 射影用シェーダープロバイダ
        struct SkyIrradianceSHShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"SkyIrradianceSH.CS.hlsl"; }
        };

        /// @brief 空キューブマップ焼き込み用シェーダープロバイダ
        struct SkyEnvironmentCaptureShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"SkyEnvironmentCapture.CS.hlsl"; }
        };

        /// @brief 空キューブマップ GGX プリフィルタ用シェーダープロバイダ
        struct SkyEnvironmentPrefilterShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"SkyEnvironmentPrefilter.CS.hlsl"; }
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
        Vector3 sunTransmittance_ = { 1.0f, 1.0f, 1.0f }; ///< 地表→太陽の大気透過率（惑星遮蔽込み）
        /// @brief CVar（"r.Atmosphere.*"）の現在値を取り込む
        /// @details SetParameters は LUT 再計算のダーティフラグを立てるため、
        ///          CVar の変更通番が動いたときだけ反映する（毎フレーム呼ぶと LUT を作り直し続ける）
        void SyncParametersFromCVars();

        /// @brief 最後に CVar を取り込んだ時点の変更通番
        uint32_t lastCVarRevision_ = 0;

        bool transmittanceOnLight_ = true; ///< 直接光へ透過率を適用するか（Transmittance on Light。太陽・月共通）

        // 月（第2大気ライト）。isAtmosphereMoon のライトが無ければ hasMoonLight_=false のまま
        Vector3 moonDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector4 moonColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        float moonIntensity_ = 0.0f;
        bool hasMoonLight_ = false;
        Vector3 moonTransmittance_ = { 1.0f, 1.0f, 1.0f }; ///< 地表→月の大気透過率（惑星遮蔽込み）
        float sceneIlluminationLuminance_ = 0.18f; ///< 照明駆動露出用のシーン代表輝度（カメラ非依存）

        bool paramsDirty_ = true;   ///< 大気パラメータ変更 → 全 LUT 再生成
        bool skyViewDirty_ = true;  ///< 太陽方向・カメラ高度変更 → Sky-View LUT のみ再生成
        bool lutsGenerated_ = false;
        bool atmosphereActive_ = false; ///< このフレームで Update() が呼ばれ大気が要求されたか
        ID3D12Device* device_ = nullptr;

        // Sky-View の変化検知用キャッシュ
        // 色・強度も対象にするのは、LUT にライト色を前乗算して格納しているため
        //（色変更を検知しないと、エディタで太陽色を動かしても空が変わらない）
        Vector3 lastSunDirection_ = { 0.0f, -1.0f, 0.0f };
        float lastCameraRadius_ = 0.0f;
        Vector4 lastSunColor_ = { -1.0f, -1.0f, -1.0f, -1.0f };
        float lastSunIntensity_ = -1.0f;
        Vector3 lastMoonDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector4 lastMoonColor_ = { -1.0f, -1.0f, -1.0f, -1.0f };
        float lastMoonIntensity_ = -1.0f;
        bool lastHasMoon_ = false;

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

        // 空アンビエント SH9 係数バッファ（9 要素の StructuredBuffer<float4>）
        Microsoft::WRL::ComPtr<ID3D12Resource> skyIrradianceSHBuffer_;
        D3D12_RESOURCE_STATES skyIrradianceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceUavHandle_{};
        bool skyIrradianceGenerated_ = false; ///< 一度でも SH が書き込まれたか
        bool skyAmbientEnabled_ = true;       ///< 空アンビエント（大気アクティブなシーンのみ効く）
        float skyAmbientScale_ = 0.3f;        ///< 空の輝度単位 → サーフェス光単位の変換係数（美術値。昼の従来アンビエントと概ね揃う値）

        // 空キューブマップ（空＋雲の作業面。mip0 のみ。α=雲透過率）
        Microsoft::WRL::ComPtr<ID3D12Resource> skyCubemap_;
        D3D12_RESOURCE_STATES skyCubemapState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE skyCubemapSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE skyCubemapUavHandle_{};

        // プリフィルタ済み空スペキュラキューブマップ（kSkySpecularMipCount ミップ）
        Microsoft::WRL::ComPtr<ID3D12Resource> skySpecularMap_;
        D3D12_RESOURCE_STATES skySpecularState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE skySpecularSrvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE skySpecularUavHandles_[kSkySpecularMipCount]{};

        // プリフィルタのミップ別パラメータ CB（kSkySpecularMipCount × 256B スロット）
        Microsoft::WRL::ComPtr<ID3D12Resource> skyPrefilterParamsCB_;

        bool skyEnvironmentGenerated_ = false; ///< 一度でもスペキュラキューブマップが生成されたか
        bool skyEnvironmentDirty_ = true;      ///< Sky-View 再生成 → 空キューブマップ再生成が必要か
        bool skySpecularEnabled_ = true;       ///< 空スペキュラIBL（大気アクティブなシーンのみ効く）
        uint64_t lastSkyEnvironmentFrame_ = UINT64_MAX; ///< View 間の二重実行ガード

        // Aerial Perspective 合成用中間テクスチャ（SceneColor と同サイズ）
        Microsoft::WRL::ComPtr<ID3D12Resource> apResult_;
        D3D12_RESOURCE_STATES apResultState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_GPU_DESCRIPTOR_HANDLE apResultUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE apResultUavCpuHandle_{};
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
        CustomShaderPipeline skyIrradiancePipeline_{};
        SkyIrradianceSHShaderProvider skyIrradianceShaderProvider_{};
        CustomShaderPipeline skyEnvironmentCapturePipeline_{};
        SkyEnvironmentCaptureShaderProvider skyEnvironmentCaptureShaderProvider_{};
        CustomShaderPipeline skyEnvironmentPrefilterPipeline_{};
        SkyEnvironmentPrefilterShaderProvider skyEnvironmentPrefilterShaderProvider_{};
        CustomShaderPipeline aerialPerspectivePipeline_{};
        AerialPerspectiveShaderProvider aerialPerspectiveShaderProvider_{};
        bool pipelinesReady_ = false;
    };
}
