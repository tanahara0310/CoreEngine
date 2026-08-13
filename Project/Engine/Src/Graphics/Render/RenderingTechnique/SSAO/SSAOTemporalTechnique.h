#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{
    /// @brief SSAO テンポラル蓄積レンダリング技術
    /// @details TAA のジッタで SSAO 入力（深度・法線）が毎フレーム半ピクセル揺れるため、
    ///          SSAO の 2 値遮蔽判定はカメラ静止時でも毎フレーム別の値を出す（＝ちらつき）。
    ///          RTShadowTemporal と同じく、モーションベクター再投影＋近傍クランプ付きの
    ///          指数移動平均で時間方向に安定させる。
    ///
    ///          SSAO 生成 → 本パス → SSAOBlur の順に実行し、
    ///          履歴は RenderTargetNames::SSAOHistoryA / B の ping-pong で保持する。
    class SSAOTemporalTechnique : public RenderingTechniqueBase {
    public:
        /// @brief シェーダー側 cbuffer SSAOTemporalParams と一致させること
        struct SSAOTemporalParams {
            float screenSize[2] = { 1280.0f, 720.0f };
            float blendAlpha = 0.1f;     ///< 現フレームの寄与率（小さいほど滑らかで遅延大）
            float disableHistory = 1.0f; ///< 1.0 で履歴無効（初回フレーム・リサイズ直後）
        };

        static constexpr Cb::Field kSSAOTemporalParamsFields[] = {
            CB_FIELD(SSAOTemporalParams, screenSize), CB_FIELD(SSAOTemporalParams, blendAlpha),
            CB_FIELD(SSAOTemporalParams, disableHistory),
        };
        CB_VERIFY_LAYOUT(SSAOTemporalParams, kSSAOTemporalParamsFields);
        CB_BIND_HLSL(SSAOTemporalParams, kSSAOTemporalParamsFields, "SSAOTemporalParams");

    public:
        SSAOTemporalTechnique() = default;
        ~SSAOTemporalTechnique() = default;

        void Initialize(DirectXCommon* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
        void OnResize(uint32_t width, uint32_t height) override;
        void DrawImGui() override;

        /// @brief 今フレームの書き込み先履歴インデックス（TAATechnique と同じ偶奇方式）
        static uint32_t GetWriteHistoryIndex(uint64_t frameNumber) { return static_cast<uint32_t>(frameNumber & 1ull); }

        /// @brief 履歴インデックスに対応するレンダーターゲット名
        static const char* GetHistoryTargetName(uint32_t index);

        /// @brief 履歴を破棄する（シーン切り替え等）
        void InvalidateHistory() { historyValid_ = false; }

        const SSAOTemporalParams& GetParams() const { return params_; }
        void SetParams(const SSAOTemporalParams& params) { params_ = params; }

    protected:
        /// @brief 有効/無効は CVar "r.SSAOTemporal.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string GetTechniqueName() const override { return "SSAOTemporal"; }
        const std::wstring& GetPixelShaderPath() const override;

    private:
        /// @brief 履歴 ping-pong ターゲットを必要に応じて生成する
        void EnsureHistoryTargets(const RenderContext& context);

        SSAOTemporalParams params_;
        FrameRingConstantBuffer cbRing_;

        bool historyValid_ = false;
        uint64_t lastExecutedFrame_ = 0;
    };
}
