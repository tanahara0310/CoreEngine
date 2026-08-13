#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>


namespace CoreEngine
{
class PostEffectGraphBuilder;

/// @brief 3D LUT カラーグレーディング（.cube ファイル対応）
/// @details パラメトリックな ColorGrading では作れない非線形・選択的なルック
///          （ティール&オレンジ等）を、DaVinci Resolve 等の外部ツールで作った
///          LUT として持ち込むための最終段グレーディング。
///          トーンマップ直後（PostTonemap 先頭）で、sRGB 表示空間の色に対して適用する。
///
///          LUT データはアップロードバッファ → CS で Texture3D へ書き込む
///          （雲ノイズ生成と同じ流儀。Texture3D の CPU 直接アップロードを避ける）。
///          未ロード時は恒等 LUT（見た目無変化）。
///          パラメータは CVar（"r.ColorLUT.*"）が唯一の保持者。
///          設計: Docs/Engine/Graphics/PostProcess/Cinematic_PostEffect_Plan.md
class ColorLUT : public PostEffectComputeBase {
public:
    /// @brief 対応する LUT 一辺の上限（.cube の一般値は 17/32/33/64）
    static constexpr uint32_t kMaxLutSize = 64;

    /// @brief 適用パスの定数（GPU レイアウト）
    struct ColorLUTParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        uint32_t lutSize      = 33;
        float    blend        = 1.0f;
    };

    static constexpr Cb::Field kColorLUTParamsFields[] = {
        CB_FIELD(ColorLUTParams, screenWidth), CB_FIELD(ColorLUTParams, screenHeight),
        CB_FIELD(ColorLUTParams, lutSize), CB_FIELD(ColorLUTParams, blend),
    };
    CB_VERIFY_LAYOUT(ColorLUTParams, kColorLUTParamsFields);
    CB_BIND_HLSL(ColorLUTParams, kColorLUTParamsFields, "ColorLUTParams");

    /// @brief 書き込みパスの定数（GPU レイアウト）
    struct FillParams {
        uint32_t lutSize = 33;
        float    pad[3]  = {};
    };

    static constexpr Cb::Field kColorLUTFillParamsFields[] = {
        CB_FIELD(FillParams, lutSize), CB_FIELD(FillParams, pad),
    };
    CB_VERIFY_LAYOUT(FillParams, kColorLUTFillParamsFields);
    CB_BIND_HLSL(FillParams, kColorLUTFillParamsFields, "FillParams");

public:
    ColorLUT() = default;
    ~ColorLUT() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整（LUT ファイルのロード UI 含む）
    void DrawImGui() override;

    /// @brief 表示色に対するルックなのでトーンカーブ通過後
    PostEffectStage GetStage() const override { return PostEffectStage::PostTonemap; }

    /// @brief LUT リソースが構築できなかったフレームはパスを積まず素通しにする
    void BuildPasses(PostEffectGraphBuilder& builder) override;

    /// @brief .cube ファイルを読み込む
    /// @param pathOrName フルパス、またはアセット名（AssetDatabase で解決を試みる）
    /// @return 成功したか。失敗理由は GetLastError() に残る
    bool LoadCubeFile(const std::string& pathOrName);

    /// @brief 恒等 LUT（見た目無変化）へ戻す
    void ResetToIdentity();

    const std::string& GetLastError() const { return lastError_; }

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "ColorLUT"; }
    std::wstring GetComputeShaderPath() const override { return L"ColorLUT.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    /// @brief Texture3D・アップロードバッファ・ビューを構築する
    bool CreateLutResources();

    /// @brief LUT 書き込みパイプライン（ColorLUTFill.CS）を構築する
    bool CreateFillPipeline();

    /// @brief アップロードバッファの内容を Texture3D へ写す（差し替え後の初回のみ）
    void RecordFillIfDirty(ID3D12GraphicsCommandList* cmdList);

    /// @brief シェーダーパスだけを差し替える最小のプロバイダ
    class ShaderProvider : public ICustomShaderProvider {
    public:
        explicit ShaderProvider(std::wstring path) : path_(std::move(path)) {}
        std::wstring GetComputeShaderPath() const override { return path_; }
    private:
        std::wstring path_;
    };

    /// @brief LUT 1 テクセル分（gLutData の要素レイアウト）
    struct LutTexel {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    };

    static constexpr Cb::Field kLutTexelFields[] = {
        CB_FIELD(LutTexel, r), CB_FIELD(LutTexel, g), CB_FIELD(LutTexel, b), CB_FIELD(LutTexel, a),
    };
    CB_VERIFY_STRIDE(LutTexel, kLutTexelFields);

    ShaderProvider fillProvider_{ L"ColorLUTFill.CS.hlsl" };
    CustomShaderPipeline fillPipeline_;

    Microsoft::WRL::ComPtr<ID3D12Resource> lutTexture_;
    D3D12_RESOURCE_STATES lutTextureState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_GPU_DESCRIPTOR_HANDLE lutSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE lutUavHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> lutDataBuffer_; ///< アップロードヒープ（永続マップ）
    LutTexel* mappedLutData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE lutDataSrvHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> colorLutParamsCB_;
    ColorLUTParams* mappedColorLutParams_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> fillParamsCB_;
    FillParams* mappedFillParams_ = nullptr;

    uint32_t lutSizeLoaded_ = 33;
    bool lutDirty_ = true;           ///< アップロードバッファの内容が Texture3D 未反映
    bool lutResourcesReady_ = false; ///< Texture3D と書き込みパイプラインが揃っているか

    std::string loadedLutName_ = "Identity";
    std::string lastError_;
    char pathInputBuffer_[512] = {};
};
}
