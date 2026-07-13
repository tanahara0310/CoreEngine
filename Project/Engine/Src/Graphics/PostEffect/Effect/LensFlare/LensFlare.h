#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief レンズフレアエフェクト（CS方式・イメージベース）
/// @details UE の Image-based Lens Flare 相当。HDR シーンから輝度抽出（1/4 解像度）→
///          画面中心対称のゴースト列＋ハロー生成（色収差付き）→ 分離ガウスブラー →
///          スターバースト変調付き加算合成の 4 パス構成。
///          Bloom の後・ToneMapping の前（HDR 空間）で実行することを前提とする。
class LensFlare : public PostEffectComputeBase {
public:
    /// @brief フレアパラメータ（HLSL 側 LensFlareParams と 80 バイトレイアウトを一致させること）
    struct LensFlareConstants {
        /// 輝度抽出閾値（HDR 空間）。この値を超えた明部だけがゴースト/ハローの源になる。
        /// 大気散乱では 空≈luminance×sunIntensity(20)。ACES が強く圧縮するため見た目が
        /// 白い地平線ヘイズでも HDR 値は x≈2〜4 に過ぎない。一方で太陽ディスクは
        /// sunDiskLuminanceScale(50)×透過率×20 ≈ 500〜900、グレア芯 ≈ 180、
        /// 太陽近傍のミー前方散乱オーラは数〜数十度に広がり x≈10〜40。
        /// 低すぎるとこのオーラ全体が源になり、太陽本体が画角外でも空全体へゴーストが散る
        /// （「太陽が無い所にも反応する」不具合の原因）。オーラ上端より高い値にして
        /// 太陽ディスク＋芯グレアだけを源にすること。
        float threshold = 28.0f;
        float softKnee = 0.5f;             ///< ソフトニー [0,1]
        float ghostDispersal = 0.6f;       ///< ゴースト間隔
        /// ゴースト強度。ゴースト色はトーン正規化（≒1）されるため、これがほぼ HDR 加算量になる。
        /// 大気の空は地平線付近で HDR 2〜4 あり、ACES 圧縮後に差が見えるには 2 前後必要。
        float ghostIntensity = 2.0f;

        float haloWidth = 0.45f;           ///< ハロー半径
        float haloIntensity = 1.0f;        ///< ハロー強度
        float chromaDistortion = 1.5f;     ///< 色収差強度
        float starburstIntensity = 0.5f;   ///< スターバースト変調 [0,1]

        float intensity = 0.6f;            ///< 全体強度
        uint32_t ghostCount = 6;           ///< ゴースト数
        uint32_t flareWidth = 0;           ///< フレアバッファ幅（Dispatch 時に自動設定）
        uint32_t flareHeight = 0;          ///< フレアバッファ高さ（Dispatch 時に自動設定）

        uint32_t screenWidth = 0;          ///< フル解像度幅（Dispatch 時に自動設定）
        uint32_t screenHeight = 0;         ///< フル解像度高さ（Dispatch 時に自動設定）
        float maxBrightness = 64.0f;       ///< 抽出輝度クランプ
        /// ガウスブラー σ。大きすぎると絞り羽根の多角形エッジが潰れて円形に戻ってしまう
        /// （六角形の平坦部の深さは半径の 13% しかない）。軽く角を丸める程度に留める。
        float blurSigma = 1.0f;

        float tint[4] = { 1.0f, 0.95f, 0.9f, 1.0f }; ///< 全体ティント

        /// 絞り羽根の枚数（多角形ゴーストの角数）。実カメラは5〜8枚が一般的、6=正六角形。
        float apertureBladeCount = 6.0f;
        float apertureRotationRad = 0.3f; ///< 絞りの回転角 [rad]（0だと辺が水平/垂直に揃って不自然）
        /// ゴースト半径（アスペクト補正済み UV 単位）。多角形/円形マスクの基準サイズ。
        /// 小さすぎると 1/4 解像度バッファ上で六角形の平坦部（半径の13%）が数ピクセルに
        /// なり、ブラーで潰れて円にしか見えなくなる。
        float ghostRadius = 0.12f;
        /// 多角形ゴーストの混在比率 [0,1]。0=全て円形（ボケた光斑）、1=全て多角形（絞り羽根形状）。
        /// 実レンズは絞り面共役の位置にできるゴーストだけが多角形になり、それ以外はボケた円形になる
        /// ため、両方混在する 0.5〜0.8 程度が最も写実的。
        float ghostPolygonMix = 0.85f;

        // ===== 太陽限定化（SetSunScreenPosition で毎フレーム設定） =====
        /// 太陽のスクリーン UV。輝度抽出をこの周辺に限定することで、加算パーティクル等の
        /// 高輝度オブジェクトがフレア源になるのを防ぐ（フレアは太陽からのみ発生）。
        float sunUv[2] = { 0.5f, 0.5f };
        float sunValid = 0.0f;        ///< 1=太陽が前方（有効）/ 0=無効（フレアなし）
        /// 抽出を許可する太陽周辺の半径（アスペクト補正済み UV 単位）。
        /// 太陽ディスク＋芯グレアを覆う程度。大きくしすぎるとミー散乱オーラまで源になる。
        float sunMaskRadius = 0.12f;
    };
    static_assert(sizeof(LensFlareConstants) == 112,
        "LensFlareConstants は HLSL 側 LensFlareParams の 112 バイトレイアウトと一致させること");

public:
    LensFlare() = default;
    ~LensFlare() = default;

    /// @brief CSエフェクト実行（内部で 4 パスをディスパッチ）
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    const LensFlareConstants& GetParams() const { return params_; }
    void SetParams(const LensFlareConstants& params) { params_ = params; }

    /// @brief 太陽のスクリーン位置を設定（毎フレーム、描画パイプラインから呼ばれる）
    /// @param u 太陽のスクリーン UV.x
    /// @param v 太陽のスクリーン UV.y
    /// @param valid 太陽がカメラ前方にある場合 true（false でフレア無効）
    void SetSunScreenPosition(float u, float v, bool valid) {
        params_.sunUv[0] = u;
        params_.sunUv[1] = v;
        params_.sunValid = valid ? 1.0f : 0.0f;
    }

protected:
    std::string  GetEffectName()        const override { return "LensFlare"; }
    std::wstring GetComputeShaderPath() const override { return L"LensFlareComposite.CS.hlsl"; }
    void OnConfigureRootSignature(RootSignatureConfig& config) override;
    void OnCreateConstantBuffers() override;

private:
    /// @brief 輝度抽出・ゴースト・ブラー用の内部パイプラインを構築する
    bool CreateInternalPipelines();

    /// @brief 1/4 解像度の中間テクスチャ群を画面サイズ追従で確保する
    bool EnsureTargets(uint32_t width, uint32_t height);

    /// @brief 定数バッファへ現在のパラメータと画面サイズを書き込む
    void UploadConstants(uint32_t width, uint32_t height);

    /// @brief 支配的な光源位置（1x1テクスチャ）を確保する
    bool EnsureSourcePosTarget();

    // ===== 内部パスのシェーダープロバイダ =====
    struct DownsampleShaderProvider final : ICustomShaderProvider {
        std::wstring GetComputeShaderPath() const override { return L"LensFlareDownsample.CS.hlsl"; }
    };
    struct FindSourceShaderProvider final : ICustomShaderProvider {
        std::wstring GetComputeShaderPath() const override { return L"LensFlareFindSource.CS.hlsl"; }
    };
    struct GhostsShaderProvider final : ICustomShaderProvider {
        std::wstring GetComputeShaderPath() const override { return L"LensFlareGhosts.CS.hlsl"; }
    };
    struct BlurShaderProvider final : ICustomShaderProvider {
        std::wstring GetComputeShaderPath() const override { return L"LensFlareBlur.CS.hlsl"; }
    };

private:
    LensFlareConstants params_;

    // 定数バッファ（永続マップ）
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsCB_;
    LensFlareConstants* mappedParams_ = nullptr;

    // ブラー方向 CB（水平/垂直で別バッファ。同一フレーム内の 2 ディスパッチで
    // 同じ CB を書き換えると GPU 実行時に後勝ちになるため分ける）
    struct BlurDirection { float dir[2]; float pad[2]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> blurDirHCB_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurDirVCB_;

    // ===== 1/4 解像度中間テクスチャ =====
    Microsoft::WRL::ComPtr<ID3D12Resource> brightBuffer_;   ///< 輝度抽出結果
    D3D12_RESOURCE_STATES brightBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_GPU_DESCRIPTOR_HANDLE brightSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE brightUavHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> featureBuffer_;  ///< ゴースト/ハロー（最終フレア）
    D3D12_RESOURCE_STATES featureBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_GPU_DESCRIPTOR_HANDLE featureSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE featureUavHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> blurBuffer_;     ///< ブラー中間（水平パス結果）
    D3D12_RESOURCE_STATES blurBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_GPU_DESCRIPTOR_HANDLE blurSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE blurUavHandle_{};

    /// 支配的な光源の UV 位置（1x1・R32G32_FLOAT）。ゴーストへ絞り羽根形状の
    /// マスクを正しい位置に合わせて重ねるための前段検出結果。
    Microsoft::WRL::ComPtr<ID3D12Resource> sourcePosBuffer_;
    D3D12_RESOURCE_STATES sourcePosBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_GPU_DESCRIPTOR_HANDLE sourcePosSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sourcePosUavHandle_{};

    uint32_t targetsWidth_ = 0;   ///< 確保済みフレアバッファの幅
    uint32_t targetsHeight_ = 0;  ///< 確保済みフレアバッファの高さ

    // ===== 内部パイプライン =====
    CustomShaderPipeline downsamplePipeline_{};
    DownsampleShaderProvider downsampleShaderProvider_{};
    CustomShaderPipeline findSourcePipeline_{};
    FindSourceShaderProvider findSourceShaderProvider_{};
    CustomShaderPipeline ghostsPipeline_{};
    GhostsShaderProvider ghostsShaderProvider_{};
    CustomShaderPipeline blurPipeline_{};
    BlurShaderProvider blurShaderProvider_{};
    bool internalPipelinesReady_ = false;
};
}
