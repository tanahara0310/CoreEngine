#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>

namespace CoreEngine
{
    /// @brief CAS (Contrast Adaptive Sharpening) レンダリング技術
    /// @details TAA 直後に置き、履歴ブレンドで落ちた解像感を戻す。
    ///          局所コントラストからシャープ量を適応的に決めるため、
    ///          単純なアンシャープマスクと違って輪郭にリンギングが出にくい。
    ///
    ///          入力は「TAA が有効なら TAA 出力 / 無効なら SceneColor」と可変なので、
    ///          論理リソース名は CASPass から注入される（SetInputResourceName）。
    class CASTechnique : public RenderingTechniqueBase {
    public:
        /// @brief シェーダー側 cbuffer CASParams と一致させること
        struct CASParams {
            float screenSize[2] = { 1280.0f, 720.0f };
            float sharpness = 0.5f; ///< 0 = 最弱 / 1 = 最強
            float pad0 = 0.0f;
        };

        static constexpr Cb::Field kCASParamsFields[] = {
            CB_FIELD(CASParams, screenSize), CB_FIELD(CASParams, sharpness), CB_FIELD(CASParams, pad0),
        };
        CB_VERIFY_LAYOUT(CASParams, kCASParamsFields);
        CB_BIND_HLSL(CASParams, kCASParamsFields, "CASParams");

    public:
        CASTechnique() = default;
        ~CASTechnique() = default;

        void Initialize(DirectXCommon* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
        void OnResize(uint32_t width, uint32_t height) override;
        void DrawImGui() override;

        /// @brief 入力の論理リソース名を設定する
        /// @details 前段（TAA の有無）で変わるため、Graph 構築時に CASPass が設定する
        /// @param name FrameBlackboard の論理リソース名
        void SetInputResourceName(const std::string& name) { inputResourceName_ = name; }

        const CASParams& GetParams() const { return params_; }
        void SetParams(const CASParams& params);
        void UpdateConstantBuffer();

    protected:
    /// @brief 有効/無効は CVar "r.CAS.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

        std::string GetTechniqueName() const override { return "CAS"; }
        const std::wstring& GetPixelShaderPath() const override;

    private:
        void CreateConstantBuffer();

        CASParams params_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        CASParams* mappedData_ = nullptr;

        std::string inputResourceName_ = FrameBlackboard::SceneColor;
    };
}
