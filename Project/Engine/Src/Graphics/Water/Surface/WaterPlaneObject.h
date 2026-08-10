#pragma once

#include "GameObject/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Graphics/Water/Surface/WaterConstantBufferSet.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief 水面表現用のグリッドメッシュオブジェクト
    /// @details PlaneMeshGenerator で N×N 分割の平面メッシュを生成し、水面専用シェーダー
    ///          （Water.VS / FFTWater.VS + Water.PS）で描画する。
    ///
    ///          **責務は「メッシュ・トランスフォーム・マテリアル・水そのもののパラメータ」に限る。**
    ///          シーンカラー / 深度 / RT 屈折・反射 / FFT / 大気といった外部リソースの結線は
    ///          WaterRenderFeature が組み立て、ApplyFrameBinding() で 1 度に渡す。
    ///          以前はこれが 12 個の setter に分かれており、「どのフレーム定数を誰がいつ書くのか」が
    ///          追えなくなっていた。
    class WaterPlaneObject : public PrimitiveGameObject
        , public ICustomShaderProvider {
    public:
        /// @param size 水面の一辺のサイズ（XZ 方向共通）
        /// @param resolution XZ 方向の分割数
        /// @param useFFTOcean true のとき FFT Ocean 描画経路を使用する
        WaterPlaneObject(float size = 50.0f, uint32_t resolution = 64, bool useFFTOcean = false);

        RenderPassType GetRenderPassType() const override {
            return RenderPassType::WaterSurface;
        }

        RenderItem BuildRenderItem() const override;

        const char* GetObjectName() const override { return "WaterPlane"; }

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
        /// @param binding WaterRenderFeature が組み立てた SRV / CBV と導出フラグ一式
        /// @details 個別 setter を置き換える単一入口。SRV の有無からシェーダー側の
        ///          参照フラグ（reflectionEnabled 等）を導出し、WaterFrameConstants を
        ///          GPU へ 1 度だけ転送する。
        void ApplyFrameBinding(const WaterFrameBinding& binding);

        // ===== simulation 層からの入力 =====

        /// @brief UV スクロールのみを更新する
        /// @param deltaTime 前フレームからの経過時間（秒）
        void UpdateUVAnimation(float deltaTime);

        /// @brief simulation 層で計算した時間を WaterConstants へ反映する
        /// @param timeSeconds 現在の simulation 時間
        void SetSimulationTime(float timeSeconds);

        // ===== 水そのもののパラメータ（UI が所有し、フレームをまたいで保持する）=====

        /// @brief UV スクロール速度を設定する（単位: UV/秒）
        void SetScrollSpeed(const Vector2& speed);

        /// @brief UV タイリング（繰り返し回数）を設定する
        void SetUVTiling(const Vector2& tiling);

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

        /// @brief 泡（whitecap）パラメータを設定する（FFTOcean 専用）
        /// @param enabled        泡合成を行うか
        /// @param bias           発生しきい値（合成ヤコビアン detJ がこれを下回ると泡が立つ）
        /// @param gain           しきい値からの立ち上がり勾配
        /// @param opacity        泡レイヤの不透明度（白ベタ回避のため 1.0 未満を推奨）
        /// @param cascadeWeights カスケード別の勾配寄与の重み（無重みは最小カスケードが支配して飽和する）
        /// @param decaySeconds   泡の寿命 τ [s]（蓄積パスの指数減衰時間）
        /// @param windCoverageScale 白波被覆率の風速追従係数（Monahan 比。1.0 = 基準風速）
        void SetFoamParameters(
            bool enabled, float bias, float gain, float opacity,
            const Vector3& cascadeWeights, float decaySeconds,
            float windCoverageScale);

        /// @brief 風速から白波被覆率の追従係数を求める（Monahan: W ∝ U^3.41）
        /// @details 基準風速（FoamBias を較正した風速）での値を 1.0 とする比。
        ///          基準より上では 1.0 で頭打ちにする — detJ 分布自体が風速とともに
        ///          広がるので、そちら側の自然な増加に任せた方が破綻しない。
        static float ComputeFoamWindCoverageScale(float windSpeed);

        /// @brief FFT Ocean 描画経路を切り替える
        void SetUseFFTOcean(bool useFFTOcean);

        // ===== マテリアル操作 =====

        /// @brief 水面ベースカラーを設定する
        void SetBaseColor(const Vector4& color);

        /// @brief 水面の Roughness を設定する
        void SetRoughness(float roughness);

        /// @brief 水面の Metallic を設定する
        void SetMetallic(float metallic);

        /// @brief IBL を有効/無効にする
        void SetIBLEnabled(bool enable);

        // ===== ゲッター =====

        /// @brief 波パラメータ配列への参照を返す（ImGui 直接編集用）
        WaveParams* GetWaves() { return waterCB_.waves; }

        /// @brief 現在有効な Gerstner Wave 本数を返す
        uint32_t GetActiveWaveCount() const { return waterCB_.activeWaveCount; }

        /// @brief メッシュのローカルサイズ（1 辺の長さ、スケール適用前）を返す
        float GetSize() const { return size_; }

        /// @brief メッシュの分割数（XZ 方向共通）を返す
        uint32_t GetResolution() const { return resolution_; }

        /// @brief DXR 屈折用に現在の WaterConstants を取得する
        const WaterConstants& GetWaterConstants() const { return waterCB_; }

        /// @brief UV スクロール速度への参照を返す（ImGui 直接編集用）
        Vector2& GetScrollSpeed() { return scrollSpeed_; }

        /// @brief UV タイリングへの参照を返す（ImGui 直接編集用）
        Vector2& GetUVTiling() { return uvTiling_; }

        /// @brief フレーム定数への参照を返す（ImGui から reflectionEnabled 等を参照する用）
        const WaterFrameConstants& GetFrameConstants() const { return frameCB_; }

        /// @brief 現在接続されている描画リソース一式を返す（診断用）
        const WaterRenderResources& GetRenderResources() const { return renderResources_; }

        /// @brief FFT Ocean 描画経路を使用中か返す
        bool IsUsingFFTOcean() const { return useFFTOcean_; }

        /// @brief FFT Ocean 用 SRV が有効か返す
        bool HasFFTOceanTextureSRVs() const {
            return renderResources_.HasFFTOceanTextureSRVs();
        }

    protected:
        std::string GetTexturePath() const override { return {}; }

        std::unique_ptr<IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

        /// @brief Initialize 完了後に独自シェーダー PSO を登録する
        void OnInitialize() override;

    private:
        /// @brief 現在の useFFTOcean 状態に合わせてカスタム PSO を再構築する
        void RebuildWaterShaderPipeline();

        /// @brief UV タイリングとオフセットをマテリアルの uvTransform 行列に反映する
        void ApplyUVTransform();

        /// @brief フレーム定数バッファを GPU へ転送する
        void UploadFrameConstants();

        float    size_;
        uint32_t resolution_;
        bool useFFTOcean_ = false;

        Vector2 scrollSpeed_; ///< UV スクロール速度（U方向, V方向）
        Vector2 uvTiling_;    ///< UV タイリング回数
        Vector2 uvOffset_;    ///< 現在の UV オフセット（内部状態）
        WaterConstantBufferSet constantBuffers_; ///< Water 描画用 GPU 定数バッファ群

        // ---- CPU 側の Water パラメータ保持 ----
        WaterConstants waterCB_{};         ///< 波パラメータと時間の CPU 側コピー
        float elapsedTime_ = 0.0f;         ///< 経過時間（波位相用）

        // ---- CPU 側のフレーム定数保持 ----
        WaterFrameConstants frameCB_{}; ///< クリップ平面や反射有効状態の CPU 側コピー

        WaterRenderResources renderResources_{};
    };
}
