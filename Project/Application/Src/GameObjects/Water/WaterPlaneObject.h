#pragma once

#include "GameObject/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "WaterConstantBufferSet.h"
#include "WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

#include <d3d12.h>

namespace CoreEngine {
    struct RenderViewResult;
}

/// @brief 水面表現用のグリッドメッシュオブジェクト
/// PlaneMeshGenerator を使用して N×N 分割の平面メッシュを生成する。
/// resolution（分割数）が高いほど後のステップで波の表現が細かくなる。
class WaterPlaneObject : public CoreEngine::PrimitiveGameObject
    , public CoreEngine::ICustomShaderProvider {
public:
    /// @param size 水面の一辺のサイズ（XZ 方向共通）
    /// @param resolution XZ 方向の分割数
    /// @param useFFTOcean true のとき新規 FFT Ocean 描画経路を使用する
    WaterPlaneObject(float size = 50.0f, uint32_t resolution = 64, bool useFFTOcean = false);

    /// @brief 水面はシャドウキャスターから除外する
    void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;

    CoreEngine::RenderPassType GetRenderPassType() const override {
        return CoreEngine::RenderPassType::WaterSurface;
    }

    CoreEngine::RenderItem BuildRenderItem() const override;

    const char* GetObjectName() const override { return "WaterPlane"; }

    // ===== ICustomShaderProvider =====
    std::wstring GetVertexShaderPath() const override;
    std::wstring GetPixelShaderPath()  const override;

    /// @brief カスタムリソース（WaterConstants CBV）をバインドする
    void BindCustomResources(
        ID3D12GraphicsCommandList* cmdList,
        const CoreEngine::CustomShaderPipeline* pipeline) const override;

    /// @brief UV スクロール速度を設定する（単位: UV/秒）
    void SetScrollSpeed(const CoreEngine::Vector2& speed);

    /// @brief UV タイリング（繰り返し回数）を設定する
    void SetUVTiling(const CoreEngine::Vector2& tiling);

    /// @brief UV スクロールと波パラメータ定数バッファを毎フレーム更新する
    /// @param deltaTime 前フレームからの経過時間（秒）
    void UpdateUVScroll(float deltaTime);

    /// @brief UV スクロールのみを更新する
    /// @param deltaTime 前フレームからの経過時間（秒）
    void UpdateUVAnimation(float deltaTime);

    /// @brief simulation 層で計算した時間を WaterConstants へ反映する
    /// @param timeSeconds 現在の simulation 時間
    void SetSimulationTime(float timeSeconds);

    /// @brief 波パラメータを設定する
    /// @param index 波インデックス（0〜15）
    /// @param wave 波パラメータ
    void SetWave(uint32_t index, const WaveParams& wave);

    /// @brief 有効な Gerstner Wave 本数を設定する
    void SetActiveWaveCount(uint32_t count);

    /// @brief 反射テクスチャの SRV を設定する（毎フレーム WaterReflectionPass から渡す）
    /// @param srvHandle 反射テクスチャの GPU ディスクリプタハンドル
    void SetReflectionTexture(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

    /// @brief クリップ平面パラメータを設定する（反射パス中に水面自体がクリップされないよう制御）
    /// @param clipPlane クリップ平面ベクトル (A, B, C, D) — dot(worldPos, plane) > 0 で描画
    /// @param enable true のとき SV_ClipDistance0 を有効化する
    void SetClipPlane(const CoreEngine::Vector4& clipPlane, bool enable);

    /// @brief フレーム定数バッファ（クリップ平面）を GPU に転送する
    void UpdateFrameConstants();

    // ===== マテリアル操作 =====

    /// @brief 水面ベースカラーを設定する
    void SetBaseColor(const CoreEngine::Vector4& color);

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

    /// @brief DXR 屈折用に現在の WaterConstants を取得する
    const WaterConstants& GetWaterConstants() const { return waterCB_; }

    /// @brief UV スクロール速度への参照を返す（ImGui 直接編集用）
    CoreEngine::Vector2& GetScrollSpeed() { return scrollSpeed_; }

    /// @brief UV タイリングへの参照を返す（ImGui 直接編集用）
    CoreEngine::Vector2& GetUVTiling() { return uvTiling_; }

    /// @brief フレーム定数への参照を返す（ImGui から reflectionEnabled 等を参照する用）
    const WaterFrameConstants& GetFrameConstants() const { return frameCB_; }

    /// @brief Fresnel の反射率パラメータを設定する
    /// @param reflectanceScale 反射ブレンドの強さ
    /// @param baseReflectance 正面入射時の反射率 F0
    void SetFresnelParameters(float reflectanceScale, float baseReflectance);

    /// @brief シーン深度テクスチャ SRV を設定する（Depth Fade 用）
    /// @param srvHandle GBuffer / DepthStencil の深度 SRV GPU ハンドル
    void SetSceneDepthSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

    /// @brief シーンカラー SRV を設定する（水面越しの透過光取得用）
    /// @param srvHandle オフスクリーンカラーの GPU ハンドル
    void SetSceneColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

    /// @brief DXR 水面屈折カラー SRV を設定する
    /// @param srvHandle DXR 屈折結果テクスチャの GPU ハンドル
    void SetRefractionColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

    /// @brief FFT Ocean の変位/法線/Jacobian テクスチャ SRV を設定する
    void SetFFTOceanTextureSRVs(
        D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle);

    /// @brief 大気散乱（Aerial Perspective）のリソースと有効フラグを設定する
    /// @param atmosphereCB AtmosphereManager 定数バッファの GPU 仮想アドレス
    /// @param cameraVolumeSrvHandle CameraVolume LUT（Texture3D）の SRV
    /// @param skyViewSrvHandle Sky-View LUT の SRV（遠距離フォールバック用）
    /// @param enabled 水面へ空気遠近感を適用するか（大気アクティブなシーンでのみ true）
    void SetAtmosphereAPResources(
        D3D12_GPU_VIRTUAL_ADDRESS atmosphereCB,
        D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE skyViewSrvHandle,
        bool enabled);

    /// @brief 空アンビエント（Sky Irradiance SH）を水中インスキャッタの天空光として接続する
    /// @param skyIrradianceSrvHandle SH9 係数バッファ（StructuredBuffer<float4> × 9）の SRV
    /// @param skyAmbientScale 空の輝度単位 → サーフェス光単位の変換係数（AtmosphereManager::GetSkyAmbientScale）
    /// @param enabled 大気アクティブ＋SH 生成済みのシーンでのみ true
    void SetSkyAmbientResources(
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSrvHandle,
        float skyAmbientScale,
        bool enabled);

    /// @brief 空スペキュラキューブマップ（空＋雲）を平面反射への雲合成用に接続する
    /// @param skyEnvironmentSrvHandle プリフィルタ済み空キューブマップ（α=雲透過率）の SRV
    /// @param enabled 大気アクティブ＋キューブマップ生成済みのシーンでのみ true
    void SetSkyEnvironmentReflection(
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSrvHandle,
        bool enabled);

    /// @brief FFT Ocean 描画経路を切り替える
    void SetUseFFTOcean(bool useFFTOcean);

    /// @brief FFT Ocean 描画経路を使用中か返す
    bool IsUsingFFTOcean() const { return useFFTOcean_; }

    /// @brief FFT Ocean 用 SRV が有効か返す
    bool HasFFTOceanTextureSRVs() const {
        return renderResources_.HasFFTOceanTextureSRVs();
    }

    /// @brief Water Reflection の出力を水面描画へ適用する
    /// @param result RenderView 出力一式
    void ApplyWaterReflectionResult(const CoreEngine::RenderViewResult& result);

    /// @brief Depth Fade の有効・無効を設定する
    /// @param enabled true のとき Depth Fade を有効にする
    void SetDepthFade(bool enabled);

    /// @brief Depth Fade のデバッグ表示を設定する
    /// @param enabled true のときデバッグ表示を有効にする
    /// @param debugScale 表示倍率
    void SetDepthFadeDebug(bool enabled, float debugScale = 1.5f);

    /// @brief 水面デバッグ可視化モードを設定する
    /// @param mode デバッグ可視化モード
    void SetDepthDebugViewMode(WaterDebugViewMode mode);

    /// @brief 水の光学特性（波長依存の吸収・散乱係数）を設定する
    /// @param absorptionCoeff 吸収係数 σa [1/m]（RGB 波長別。赤 > 緑 > 青 が自然な水）
    /// @param scatteringCoeff 散乱係数 σs [1/m]（RGB 波長別。深瀬のインスキャッタ色を決める）
    void SetWaterOpticalCoefficients(const CoreEngine::Vector3& absorptionCoeff, const CoreEngine::Vector3& scatteringCoeff);

protected:
    std::string GetTexturePath() const override { return {}; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

    /// @brief Initialize 完了後に独自シェーダー PSO を登録する
    void OnInitialize() override;

private:
    /// @brief 現在の useFFTOcean 状態に合わせてカスタム PSO を再構築する
    void RebuildWaterShaderPipeline();

    /// @brief UV タイリングとオフセットをマテリアルの uvTransform 行列に反映する
    void ApplyUVTransform();

    float    size_;
    uint32_t resolution_;
    bool useFFTOcean_ = false;

    CoreEngine::Vector2 scrollSpeed_; ///< UV スクロール速度（U方向, V方向）
    CoreEngine::Vector2 uvTiling_;    ///< UV タイリング回数
    CoreEngine::Vector2 uvOffset_;    ///< 現在の UV オフセット（内部状態）
    WaterConstantBufferSet constantBuffers_; ///< Water 描画用 GPU 定数バッファ群

    // ---- CPU 側の Water パラメータ保持 ----
    WaterConstants waterCB_{};         ///< 波パラメータと時間の CPU 側コピー
    float elapsedTime_ = 0.0f;         ///< 経過時間（波位相用）

    // ---- CPU 側のフレーム定数保持 ----
    WaterFrameConstants frameCB_{}; ///< クリップ平面や反射有効状態の CPU 側コピー

    WaterRenderResources renderResources_{};
};
