#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "WaterConstantBuffer.h"

#include <d3d12.h>
#include <wrl.h>

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

    /// @brief FFT Ocean 描画経路を切り替える
    void SetUseFFTOcean(bool useFFTOcean);

    /// @brief FFT Ocean 描画経路を使用中か返す
    bool IsUsingFFTOcean() const { return useFFTOcean_; }

    /// @brief FFT Ocean 用 SRV が有効か返す
    bool HasFFTOceanTextureSRVs() const {
        return fftDisplacementSRV_.ptr != 0 && fftNormalSRV_.ptr != 0;
    }

    /// @brief Water Reflection の出力を水面描画へ適用する
    /// @param result RenderView 出力一式
    void ApplyWaterReflectionResult(const CoreEngine::RenderViewResult& result);

    /// @brief Depth Fade パラメータを設定する
    /// @param absorptionCoeff 光吸収係数（大きいほど短距離で不透明になる）
    /// @param enabled true のとき Depth Fade を有効にする
    void SetDepthFade(float absorptionCoeff, bool enabled);

    /// @brief Depth Fade のデバッグ表示を設定する
    /// @param enabled true のときデバッグ表示を有効にする
    /// @param debugScale 表示倍率
    void SetDepthFadeDebug(bool enabled, float debugScale = 1.5f);

    /// @brief 水面デバッグ可視化モードを設定する
    /// @param mode デバッグ可視化モード
    void SetDepthDebugViewMode(WaterDebugViewMode mode);

    /// @brief 浅瀬と深場の水色を設定する（Depth Fade と連動）
    void SetWaterColors(const CoreEngine::Vector3& shallowColor, const CoreEngine::Vector3& deepColor);

protected:
    std::string GetTexturePath() const override { return {}; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

    /// @brief Initialize 完了後に独自シェーダー PSO を登録する
    void OnInitialize() override;

private:
    /// @brief 定数バッファリソースを作成する（OnInitialize 内から呼ぶ）
    void CreateWaterConstantBuffer(ID3D12Device* device);

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

    // ---- 波パラメータ定数バッファ（b4 にバインド） ----
    WaterConstants waterCB_;                                   ///< CPU 側バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> waterCBResource_;  ///< GPU リソース
    D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress_ = 0;         ///< GPU 仮想アドレス
    uint8_t* waterCBMapped_ = nullptr;                        ///< マップ済みポインタ
    float elapsedTime_ = 0.0f;                                ///< 経過時間（波位相用）

    // ---- フレーム定数バッファ（b5 にバインド）: クリップ平面 ----
    WaterFrameConstants frameCB_;                                    ///< CPU 側バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> frameCBResource_;         ///< 通常描画用 GPU リソース
    D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress_ = 0;                ///< 通常描画用 GPU 仮想アドレス
    uint8_t* frameCBMapped_ = nullptr;                               ///< 通常描画用マップ済みポインタ
    Microsoft::WRL::ComPtr<ID3D12Resource> reflectionFrameCBResource_; ///< 反射パス用 GPU リソース
    D3D12_GPU_VIRTUAL_ADDRESS reflectionFrameCBGpuAddress_ = 0;        ///< 反射パス用 GPU 仮想アドレス
    uint8_t* reflectionFrameCBMapped_ = nullptr;                       ///< 反射パス用マップ済みポインタ

    // ---- 反射テクスチャ SRV（t14 にバインド） ----
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionSRV_ = { 0 };              ///< 反射 RTT の SRV

    // ---- シーン深度テクスチャ SRV（t15 にバインド）: Depth Fade 用 ----
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV_ = { 0 };              ///< 深度 SRV

    // ---- シーンカラー SRV（t16 にバインド）: 水越しの背景色用 ----
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV_ = { 0 };              ///< シーンカラー SRV

    // ---- DXR 屈折カラー SRV（t17 にバインド） ----
    D3D12_GPU_DESCRIPTOR_HANDLE refractionColorSRV_ = { 0 };         ///< DXR 屈折カラー SRV

    // ---- FFT Ocean の変位/法線/Jacobian SRV（t18/t19/t20 にバインド） ----
    D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV_ = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV_ = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE fftJacobianSRV_ = { 0 };
};
