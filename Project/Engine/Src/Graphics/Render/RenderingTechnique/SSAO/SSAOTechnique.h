#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief SSAO (Screen Space Ambient Occlusion) レンダリング技術
    /// @details GBufferから法線・深度・ワールド座標を使用してAOを計算
    class SSAOTechnique : public RenderingTechniqueBase {
    public:
        /// @brief シェーダー側 cbuffer SSAOParams と一致させること
        struct SSAOParams {
            float viewMatrix[16] = {};
            float projectionMatrix[16] = {};
            float invViewProjMatrix[16] = {}; // WorldPosition ターゲット廃止に伴う深度復元用
            float radius = 0.5f;
            float bias = 0.025f;
            float intensity = 1.0f;
            int   sampleCount = 16;
            float screenWidth = 1280.0f;
            float screenHeight = 720.0f;
            float power = 1.5f;
            float _pad0 = 0.0f;
        };

        static constexpr Cb::Field kSSAOParamsFields[] = {
            CB_FIELD_AS(SSAOParams, viewMatrix, Cb::Float4x4),
            CB_FIELD_AS(SSAOParams, projectionMatrix, Cb::Float4x4),
            CB_FIELD_AS(SSAOParams, invViewProjMatrix, Cb::Float4x4), CB_FIELD(SSAOParams, radius),
            CB_FIELD(SSAOParams, bias), CB_FIELD(SSAOParams, intensity), CB_FIELD(SSAOParams, sampleCount),
            CB_FIELD(SSAOParams, screenWidth), CB_FIELD(SSAOParams, screenHeight), CB_FIELD(SSAOParams, power),
            CB_FIELD(SSAOParams, _pad0),
        };
        CB_VERIFY_LAYOUT(SSAOParams, kSSAOParamsFields);
        CB_BIND_HLSL(SSAOParams, kSSAOParamsFields, "SSAOParams");

    public:
        SSAOTechnique() = default;
        ~SSAOTechnique() = default;

        void Initialize(GraphicsCore* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
        void OnResize(uint32_t width, uint32_t height) override;
        void DrawImGui() override;

        const SSAOParams& GetParams() const { return params_; }
        void SetParams(const SSAOParams& params) { params_ = params; }

    protected:
        /// @brief 有効/無効は CVar "r.SSAO.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string GetTechniqueName() const override { return "SSAO"; }
        const std::wstring& GetPixelShaderPath() const override;

    private:
        SSAOParams params_;
        // 定数は UploadRing（GraphicsCore::GetUploadRing）から毎フレーム確保する。
        // 単一バッファ上書きだと GPU 実行中フレームの行列を CPU が書き潰し、
        // 深度バッファと行列が食い違って AO が毎フレームちらついた。
    };
}
