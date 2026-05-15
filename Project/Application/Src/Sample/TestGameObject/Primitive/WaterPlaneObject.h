#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"
#include "WaterConstantBuffer.h"

#include <d3d12.h>
#include <wrl.h>

/// @brief 水面表現用のグリッドメッシュオブジェクト
/// PlaneMeshGenerator を使用して N×N 分割の平面メッシュを生成する。
/// resolution（分割数）が高いほど後のステップで波の表現が細かくなる。
class WaterPlaneObject : public CoreEngine::PrimitiveGameObject
                       , public CoreEngine::ICustomShaderProvider {
public:
    /// @param size 水面の一辺のサイズ（XZ 方向共通）
    /// @param resolution XZ 方向の分割数
    /// @param albedoTextureName アルベドテクスチャのファイル名（空文字列の場合は単色）
    WaterPlaneObject(float size = 50.0f, uint32_t resolution = 64,
        const std::string& albedoTextureName = {});

    const char* GetObjectName() const override { return "WaterPlane"; }

    // ===== ICustomShaderProvider =====
    std::wstring GetVertexShaderPath() const override { return L"Water.VS.hlsl"; }
    std::wstring GetPixelShaderPath()  const override { return L"Water.PS.hlsl"; }

    /// @brief カスタムリソース（WaterConstants CBV）をバインドする
    void BindCustomResources(
        ID3D12GraphicsCommandList* cmdList,
        const CoreEngine::CustomShaderPipeline* pipeline) const override;

    /// @brief ノーマルマップテクスチャのファイル名を設定する（Initialize 後に呼ぶこと）
    void SetNormalMapTextureName(const std::string& fileName);

    /// @brief UV スクロール速度を設定する（単位: UV/秒）
    void SetScrollSpeed(const CoreEngine::Vector2& speed);

    /// @brief UV タイリング（繰り返し回数）を設定する
    void SetUVTiling(const CoreEngine::Vector2& tiling);

    /// @brief UV スクロールと波パラメータ定数バッファを毎フレーム更新する
    /// @param deltaTime 前フレームからの経過時間（秒）
    void UpdateUVScroll(float deltaTime);

    /// @brief 波パラメータを設定する
    /// @param index 波インデックス（0〜3）
    /// @param wave 波パラメータ
    void SetWave(uint32_t index, const WaveParams& wave);

protected:
    std::string GetTexturePath() const override { return albedoTextureName_; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

    /// @brief Initialize 完了後に独自シェーダー PSO を登録する
    void OnInitialize() override;

private:
    /// @brief 定数バッファリソースを作成する（OnInitialize 内から呼ぶ）
    void CreateWaterConstantBuffer(ID3D12Device* device);

    /// @brief UV タイリングとオフセットをマテリアルの uvTransform 行列に反映する
    void ApplyUVTransform();

    float    size_;
    uint32_t resolution_;

    std::string albedoTextureName_;   ///< アルベドテクスチャのファイル名

    CoreEngine::Vector2 scrollSpeed_; ///< UV スクロール速度（U方向, V方向）
    CoreEngine::Vector2 uvTiling_;    ///< UV タイリング回数
    CoreEngine::Vector2 uvOffset_;    ///< 現在の UV オフセット（内部状態）

    // ---- 波パラメータ定数バッファ（b4 にバインド） ----
    WaterConstants waterCB_;                                   ///< CPU 側バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> waterCBResource_;  ///< GPU リソース
    D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress_ = 0;         ///< GPU 仮想アドレス
    uint8_t* waterCBMapped_ = nullptr;                        ///< マップ済みポインタ
    float elapsedTime_ = 0.0f;                                ///< 経過時間（波位相用）
};
