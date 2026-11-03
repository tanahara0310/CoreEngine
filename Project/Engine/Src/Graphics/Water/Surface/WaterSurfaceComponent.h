#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Graphics/Water/Surface/WaterConstantBufferSet.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/WaterProfile.h"

#include <array>
#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "Graphics/Water/Surface/WaterShaderResourceBinder.h"
#include "Reflection/Reflect.h"

#include <d3d12.h>
#include <memory>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief 水面を描くコンポーネント（平面メッシュ・水の性質・専用シェーダー）
    /// @details 責務はメッシュ・マテリアルと水そのものの性質（WaterProfile）まで。
    ///          水の性質はシーンへ保存し、`.waterprofile` を指していれば読み込むときにその値で上書きする。
    ///          シーンカラーや RT 屈折・FFT・大気といった外部リソースの結線は
    ///          WaterSystem が組み立て、ApplyFrameBinding() で 1 度に渡す。
    ///          トランスフォームとメッシュ描画のコンポーネントは `Awake()` で足す。
    class WaterSurfaceComponent : public IComponent
        , public ICustomShaderProvider {
    public:
        /// @param size 水面の一辺のサイズ（XZ 方向共通）
        /// @param resolution XZ 方向の分割数
        /// @note 描画経路（FFT か Gerstner か）は WaterProfile::fftEnabled から決める。
        WaterSurfaceComponent(float size = 50.0f, uint32_t resolution = 64);

        const char* GetTypeName() const override { return "WaterSurface"; }

        // 水面 1 枚ごとの値（大きさ・分割数・潮）と、水の性質（WaterProfile）。
        // 風はシーンの r.Wind.*、描画の品質と負荷はプロジェクトの r.Water.* が持つ
        REFLECT_BEGIN(WaterSurfaceComponent, "水面")
            REFLECT_PROPERTY(size_, "一辺の長さ", p.range = Range(1.0f, 20000.0f, 1.0f),
                p.tooltip = "メッシュの大きさ [m]。シーンを読み込むときに効く")
            REFLECT_ACCESSOR("resolution", "分割数", GetResolutionValue, SetResolutionValue,
                p.range = Range(2.0f, 1024.0f, 1.0f),
                p.tooltip = "XZ 方向の分割数。シーンを読み込むときに効く")
            REFLECT_PROPERTY(primary_, "主の水にする",
                p.tooltip = "FFT の海・泡・岸・レイトレーシングの映り込みと屈折・濡れ跡・水中の霧をこの水面で描く。"
                            "どの水面にも付いていなければシーンで最初の水面。ほかの水面は Gerstner の波と空の映り込みで描く")
            REFLECT_PROPERTY(tideAmplitude_, "潮の高さ", p.range = Range(0.0f, 20.0f, 0.01f),
                p.tooltip = "月と太陽が真上か真下に来たとき、水面がトランスフォームの高さより上がる高さ [m]。"
                            "地平線に来たときは同じだけ下がる。0 で潮なし")
            REFLECT_READONLY_ACCESSOR("tideHeight", "今の潮位", GetTideHeight)
            REFLECT_READONLY_ACCESSOR("waterLevel", "今の水面の高さ", GetWaterLevel)

            REFLECT_ASSET_REF(profileAsset_, "プロファイル")

            // ---- 見た目 ----
            REFLECT_PROPERTY(profile_.baseColor, "反射が無いときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.tooltip = "レイトレーシングの映り込みも空のキューブも無いときだけ使う色（不透明度は使わない）")
            REFLECT_PROPERTY(profile_.roughness, "粗さ", p.range = Range(0.0f, 1.0f),
                p.tooltip = "水面の粗さ（ハイライトの広がり）")
            REFLECT_PROPERTY(profile_.fresnelScale, "反射の強さ", p.range = Range(0.0f, 2.0f),
                p.tooltip = "反射の強さの倍率（1 = 物理値）")
            REFLECT_PROPERTY(profile_.fresnelF0, "正面の反射率", p.range = Range(0.0f, 0.1f),
                p.tooltip = "正面から見たときの反射率（水は 0.02）")
            REFLECT_PROPERTY(profile_.depthFadeEnabled, "水深で透け方を変える",
                p.tooltip = "水の厚さで光が減る分（Beer-Lambert）を透け方に入れる")

            // ---- 水質 ----
            REFLECT_PROPERTY(profile_.absorption, "吸収 [1/m]", p.range = Range(0.0f, 4.0f),
                p.tooltip = "吸収係数 σa（RGB）。澄んだ水は 赤 > 緑 > 青")
            REFLECT_PROPERTY(profile_.scattering, "散乱 [1/m]", p.range = Range(0.0f, 2.0f),
                p.tooltip = "散乱係数 σs（RGB）。深いところの水の色を決める")
            REFLECT_PROPERTY(profile_.turbidity, "濁り", p.range = Range(0.0f, 1.0f),
                p.tooltip = "青の吸収と粒子の散乱を足す（緑に濁る）")
            REFLECT_PROPERTY(profile_.refractiveIndex, "屈折率", p.range = Range(1.0f, 2.0f),
                p.tooltip = "空気 1 に対する屈折率（水は 1.333）。水面の透過・屈折・コースティクス・水中の影に効く")

            // ---- 風波 ----
            REFLECT_PROPERTY(profile_.fftEnabled, "FFT の海で描く",
                p.tooltip = "切ると Gerstner の波で描く（Gerstner の波は保存しない）")
            REFLECT_PROPERTY(profile_.fetchKm, "吹送距離 [km]", p.range = Range(1.0f, 2000.0f),
                p.tooltip = "風が海の上を吹いてきた距離。シーンの風速と合わせて波の長さと高さが決まる。"
                            "短いと若く尖った波、長いと育ちきった波")
            REFLECT_PROPERTY(profile_.amplitudeScale, "高さの倍率", p.range = Range(0.0f, 4.0f),
                p.tooltip = "FFT の海では風速と吹送距離から求めた波高に、Gerstner の波では 16 本の波の高さに掛ける倍率（0 で波なし）")
            REFLECT_PROPERTY(profile_.choppiness, "波頭の尖り", p.range = Range(0.0f, 4.0f),
                p.tooltip = "水平に寄せる強さ")
            REFLECT_PROPERTY(profile_.componentCount, "短い波の量", p.range = Range(1.0f, 64.0f),
                p.tooltip = "スペクトルの成分の数（多いほど短い波まで入る）")

            // ---- うねり ----
            REFLECT_PROPERTY(profile_.swellEnabled, "うねりを重ねる",
                p.tooltip = "遠くの嵐から届いた長い周期の波を重ねる（その場の風とは別に決まる）")
            REFLECT_PROPERTY(profile_.swellHeight, "うねりの高さ [m]", p.range = Range(0.0f, 12.0f),
                p.tooltip = "有義波高")
            REFLECT_PROPERTY(profile_.swellPeriod, "うねりの周期 [s]", p.range = Range(2.0f, 30.0f),
                p.tooltip = "ピーク周期（波長 = 重力 × 周期² / 2π。12 秒で約 225 m）")
            REFLECT_PROPERTY(profile_.swellDirection, "うねりの進む向き",
                p.flags = ::CoreEngine::Reflection::PropertyFlags::Direction,
                p.tooltip = "+X から +Z へ回した角度。風の向きと変えると斜めに交わる波になる")
            REFLECT_PROPERTY(profile_.swellRelativeWidth, "うねりの周期のばらつき", p.range = Range(0.01f, 0.5f),
                p.tooltip = "周波数の帯域幅（ピークに対する比）。小さいほど周期がそろう")
            REFLECT_PROPERTY(profile_.swellSpread, "うねりの向きのそろい方", p.range = Range(1.0f, 60.0f),
                p.tooltip = "大きいほど一方向にそろう")

            // ---- 泡 ----
            REFLECT_PROPERTY(profile_.foamEnabled, "泡を出す",
                p.tooltip = "白波と岸の泡を出す（FFT の海だけ）")
            REFLECT_PROPERTY(profile_.foamWhitecapScale, "白波の量", p.range = Range(0.0f, 4.0f),
                p.tooltip = "白波の割合の倍率（1 = 風速からの観測式 W = 3.84e-6·U^3.41）")
            REFLECT_PROPERTY(profile_.foamOpacity, "泡の不透明度", p.range = Range(0.0f, 1.0f))
            REFLECT_PROPERTY(profile_.foamDecaySeconds, "泡が消えるまでの時間 [s]", p.range = Range(0.2f, 10.0f),
                p.tooltip = "泡が e^-1 まで減る時間")
            REFLECT_PROPERTY(profile_.foamCascadeWeights, "波の大きさごとの重み", p.range = Range(0.0f, 1.0f),
                p.tooltip = "白波を立てる傾きの寄与（大・中・小のカスケード）")

            REFLECT_METHOD("SetTideHeight", "潮位を指定する", SetTideHeight,
                m.tooltip = "月と太陽の代わりに、この高さ [m] を潮位にする。ClearTideHeight で戻す")
            REFLECT_METHOD("ClearTideHeight", "潮位の指定をやめる", ClearTideHeight,
                m.tooltip = "月と太陽から潮位を求める状態へ戻す")
        REFLECT_END()

        /// @brief トランスフォームとメッシュ描画を確保し、平面メッシュ・シェーダー・定数バッファを用意する
        /// @details プロファイルを指していれば、その値を読み込む。
        void Awake() override;

        /// @brief 指すプロファイルが変わったら読み直す
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        /// @brief トランスフォームとメッシュ描画を使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief トランスフォームを取得
        const WorldTransform& GetTransform() const { return transformComponent_->Get(); }

        /// @brief 水面のメッシュのモデル（まだ作っていなければ nullptr）
        Model* GetModel() const { return meshRenderer_->GetModel(); }

        /// @brief メッシュ描画コンポーネントを取得する
        MeshRendererComponent* GetMeshRenderer() const { return meshRenderer_; }

        // ===== ICustomShaderProvider =====
        std::wstring GetVertexShaderPath() const override;
        std::wstring GetPixelShaderPath()  const override;

        /// @brief 水面は SV_TARGET1 へモーションベクターを書く
        /// @details 水面は GBuffer より後のフォワードパスなので、書かないと TAA が
        ///          「水の背後の地形」のモーションベクターで履歴を再投影し、カメラ移動中
        ///          だけ泡などの高周波が溶けてぼける。WaterSurfacePass の
        ///          OMSetRenderTargets（2 枚）と必ず対で維持すること。
        bool WritesMotionVector() const override;

        /// @brief カスタムリソース（WaterConstants CBV）をバインドする
        void BindCustomResources(
            ID3D12GraphicsCommandList* cmdList,
            const CustomShaderPipeline* pipeline) const override;

        // ===== Engine 側からのフレーム結線（唯一の入口）=====

        /// @brief このフレームの外部リソース結線をまとめて適用し、GPU へ転送する
        /// @details 個別 setter を置き換える単一入口。SRV の有無からシェーダー側の参照フラグを導出する。
        void ApplyFrameBinding(const WaterFrameBinding& binding);

        // ===== simulation 層からの入力 =====

        /// @brief simulation 層で計算した時間を WaterConstants へ反映する
        /// @param timeSeconds 現在の simulation 時間
        void SetSimulationTime(float timeSeconds);

        // ===== 水そのもののパラメータ（UI が所有し、フレームをまたいで保持する）=====

        /// @brief 波パラメータを設定する
        /// @param index 波インデックス（0〜15）
        /// @param wave 波パラメータ
        void SetWave(uint32_t index, const WaveParams& wave);

        /// @brief 有効な Gerstner Wave 本数を設定する
        void SetActiveWaveCount(uint32_t count);

        /// @brief Fresnel の反射率パラメータを設定する
        /// @param reflectanceScale 反射ブレンドの強さ
        /// @param baseReflectance 正面入射時の反射率 F0
        void SetFresnelParameters(float reflectanceScale, float baseReflectance);

        /// @brief Depth Fade の有効・無効を設定する
        void SetDepthFade(bool enabled);

        /// @brief Depth Fade のデバッグ表示を設定する
        /// @param enabled true のときデバッグ表示を有効にする
        /// @param debugScale 表示倍率
        void SetDepthFadeDebug(bool enabled, float debugScale = 1.5f);

        /// @brief 水面デバッグ可視化モードを設定する
        void SetDepthDebugViewMode(WaterDebugViewMode mode);

        /// @brief 水の光学特性（波長依存の吸収・散乱係数）を設定する
        /// @param absorptionCoeff 吸収係数 σa [1/m]（RGB 波長別。赤 > 緑 > 青 が自然な水）
        /// @param scatteringCoeff 散乱係数 σs [1/m]（RGB 波長別。深瀬のインスキャッタ色を決める）
        void SetWaterOpticalCoefficients(const Vector3& absorptionCoeff, const Vector3& scatteringCoeff);

        /// @brief 水の屈折率（空気 1 に対する比）を設定する
        void SetRefractiveIndex(float refractiveIndex);

        /// @brief 泡（whitecap）パラメータを設定する（FFTOcean 専用）
        /// @param bias           砕けるしきい値（合成ヤコビアン detJ がこれを下回ると泡が立つ）
        /// @param cascadeWeights カスケード別の勾配寄与の重み（無重みだと最小カスケードが支配して飽和する）
        void SetFoamParameters(
            bool enabled, float bias, float gain, float opacity,
            const Vector3& cascadeWeights, float decaySeconds);

        /// @brief 波群エンベロープの位相のずれ [rad] を設定する（FFTOceanSpectrum::ComputeWaveGroupPhase）
        void SetWaveGroupPhase(const std::array<float, 3>& phase);

        /// @brief 白波の泡の動きを設定する
        /// @param driftOffsetXZ 泡が風下へ流れた距離 [m]（WaterWhitecapFoam::ComputeDriftOffset）
        /// @param stretchAxis   模様を風の向きに伸ばす軸（WaterWhitecapFoam::ComputeStretchAxis）
        void SetFoamMotion(const std::array<float, 2>& driftOffsetXZ, const std::array<float, 2>& stretchAxis);

        /// @brief 描く波のモデルを切り替える（シェーダーを作り直す）
        void SetWaveModel(WaterWaveModelType waveModel);

        // ===== 水の性質 =====

        /// @brief 水の性質（見た目・水質・波・うねり・泡）
        const WaterProfile& GetProfile() const { return profile_; }

        /// @brief 水の性質をまとめて入れる（プロファイルの指す先は変えない）
        void SetProfile(const WaterProfile& profile) { profile_ = profile; }

        /// @brief 指しているプロファイル（何も指していなければ自分の値を使う）
        const AssetRef<WaterProfileAsset>& GetProfileAsset() const { return profileAsset_; }

        /// @brief プロファイルを指し直して読み込む（空なら指さない）
        void SetProfileAsset(std::string_view pathOrName);

        /// @brief 指しているプロファイルの値を自分へ写す
        /// @return 読めたら true（指していない・読めなければ false で、値は変えない）
        bool LoadProfileFromAsset();

        /// @brief 指しているプロファイルの値を、保存形で読む（水面 1 枚ごとの値は含まない）
        /// @return 指していない・読めなければ null の JSON
        json ReadProfileAsset() const;

        /// @brief 今の水の性質を、指しているプロファイルへ書き込む
        /// @return 書けたら true
        bool SaveProfileToAsset() const;

        /// @brief 今の水の性質をプロファイルのファイルとして書く（指す先は変えない）
        /// @param assetPath `Application/Assets/…` の綴り（拡張子は .waterprofile）
        /// @return 書けたら true
        bool WriteProfile(const std::string& assetPath) const;

        // ===== 主の水 =====

        /// @brief 主の水にするか（FFT の海・泡・岸・レイトレーシングの映り込みと屈折をこの水面で描く）
        bool IsPrimary() const { return primary_; }

        /// @brief 主の水にするかを設定する
        void SetPrimary(bool primary) { primary_ = primary; }

        // ===== 潮 =====

        /// @brief 潮の高さ（月と太陽が真上か真下に来たときにトランスフォームの高さより上がる高さ [m]。0 で潮なし）
        float GetTideAmplitude() const { return tideAmplitude_; }

        /// @brief 月と太陽から求めた潮位 [m] を受け取る（WaterSystem が毎フレーム呼ぶ）
        void SetSkyTideHeight(float height) { skyTideHeight_ = height; }

        /// @brief 今の潮位 [m]（SetTideHeight で指定していればその値、なければ月と太陽から求めた値）
        float GetTideHeight() const { return tideOverridden_ ? tideOverrideHeight_ : skyTideHeight_; }

        /// @brief 今の水面の高さ [m]（トランスフォームの高さ＋今の潮位）
        float GetWaterLevel() const;

        /// @brief 月と太陽の代わりに潮位 [m] を指定する
        void SetTideHeight(float height);

        /// @brief 潮位の指定をやめ、月と太陽から求める状態へ戻す
        void ClearTideHeight();

        // ===== マテリアル操作 =====

        /// @brief 水面ベースカラーを設定する
        void SetBaseColor(const Vector4& color);

        /// @brief 水面の Roughness を設定する
        void SetRoughness(float roughness);

        // ===== ゲッター =====

        /// @brief 波パラメータ配列への参照を返す（ImGui 直接編集用。高さの倍率を掛ける前の値）
        WaveParams* GetWaves() { return waterCB_.waves; }

        /// @brief 現在有効な Gerstner Wave 本数を返す
        uint32_t GetActiveWaveCount() const { return waterCB_.activeWaveCount; }

        /// @brief メッシュのローカルサイズ（1 辺の長さ、スケール適用前）を返す
        float GetSize() const { return size_; }

        /// @brief メッシュの分割数（XZ 方向共通）を返す
        uint32_t GetResolution() const { return resolution_; }

        /// @brief 粗い格子の 1 マスのワールドの大きさ（X, Z）[m]
        Vector2 GetMeshCellSizeXZ() const;

        /// @brief 分割数（記述子が扱える int で読み書きする）
        int GetResolutionValue() const { return static_cast<int>(resolution_); }
        void SetResolutionValue(int resolution) { resolution_ = static_cast<uint32_t>(resolution < 2 ? 2 : resolution); }

        /// @brief 描いている WaterConstants（Gerstner の波の高さに高さの倍率を掛けた値）
        const WaterConstants& GetWaterConstants() const { return drawnWaterCB_; }

        /// @brief フレーム定数への参照を返す（ImGui から reflectionEnabled 等を参照する用）
        const WaterFrameConstants& GetFrameConstants() const { return frameCB_; }

        /// @brief 現在接続されている描画リソース一式を返す（診断用）
        const WaterRenderResources& GetRenderResources() const { return renderResources_; }

        /// @brief 描いている波のモデル
        WaterWaveModelType GetWaveModel() const { return waveModel_; }

        /// @brief FFT Ocean 用 SRV が有効か返す
        bool HasFFTOceanTextureSRVs() const {
            return renderResources_.HasFFTOceanTextureSRVs();
        }

    private:
        /// @brief 今の水の性質を、プロファイルの保存形（水面 1 枚ごとの値を除いたもの）にする
        json BuildProfileJson() const;

        /// @brief 水面の平面メッシュの生成器を作る
        std::unique_ptr<IPrimitiveMeshGenerator> CreateMeshGenerator() const;

        /// @brief 今の波のモデルに合わせてカスタム PSO を再構築する
        void RebuildWaterShaderPipeline();

        /// @brief フレーム定数バッファを GPU へ転送する
        void UploadFrameConstants();

        /// @brief 波の値に高さの倍率を掛けて描く値を作り、GPU へ転送する
        void UploadWaterConstants();

        TransformComponent* transformComponent_ = nullptr;
        MeshRendererComponent* meshRenderer_ = nullptr;

        float    size_;
        uint32_t resolution_;
        WaterWaveModelType waveModel_ = WaterProfile{}.WaveModel();

        bool primary_ = false;                     ///< 主の水にするか
        AssetRef<WaterProfileAsset> profileAsset_; ///< 値を読み込むプロファイル（何も指さなければ自分の値）
        WaterProfile profile_{};                   ///< 水の性質

        float tideAmplitude_ = 0.0f;      ///< 月と太陽が真上か真下に来たときにトランスフォームの高さより上がる高さ [m]
        float skyTideHeight_ = 0.0f;      ///< 月と太陽から求めた潮位 [m]
        bool tideOverridden_ = false;     ///< SetTideHeight で潮位を指定しているか
        float tideOverrideHeight_ = 0.0f; ///< SetTideHeight で指定した潮位 [m]

        WaterConstantBufferSet constantBuffers_; ///< Water 描画用 GPU 定数バッファ群

        /// @brief 水面固有リソースのバインダ（宣言表の解決結果をキャッシュする）
        /// @note BindCustomResources() が const なので mutable。キャッシュ更新のみに使う
        mutable WaterShaderResourceBinder shaderResourceBinder_;

        // ---- CPU 側の Water パラメータ保持 ----
        WaterConstants waterCB_{};         ///< 波パラメータと時間の CPU 側コピー
        WaterConstants drawnWaterCB_{};    ///< 描く値（waterCB_ の波の高さに高さの倍率を掛けたもの）
        float elapsedTime_ = 0.0f;         ///< 経過時間（波位相用）

        // ---- CPU 側のフレーム定数保持 ----
        WaterFrameConstants frameCB_{}; ///< クリップ平面や反射有効状態の CPU 側コピー

        WaterRenderResources renderResources_{};
    };
}
