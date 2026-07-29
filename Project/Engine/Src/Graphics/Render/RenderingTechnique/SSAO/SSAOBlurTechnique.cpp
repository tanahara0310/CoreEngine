#include "pch.h"
#include "SSAOBlurTechnique.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/RenderManager.h"
#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Scene/SceneManager.h"
#include "Math/MathCore.h"
#include <cstring>
#include <cassert>

namespace CoreEngine
{
    void SSAOBlurTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        cbRing_.Initialize(dxCommon, sizeof(SSAOBlurParams));
    }

    void SSAOBlurTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        if (!IsEnabled() || !context.gBufferManager || !context.renderTargetManager) {
            outputSrvHandle = {};
            return;
        }

        auto* gBufferManager = context.gBufferManager;
        auto* renderTargetManager = context.renderTargetManager;
        auto* cmdList = context.dxCommon->GetCommandList();

        // スクリーンサイズの更新
        const float w = static_cast<float>(gBufferManager->GetWidth());
        const float h = static_cast<float>(gBufferManager->GetHeight());
        params_.screenWidth = w;
        params_.screenHeight = h;

        // 深度復元用 View*Projection 逆行列（ビューごとに更新）
        // 実際に描画へ使われたカメラから取ること（SSAOTechnique と同じ理由）。
        // 誤った行列だとワールド座標差の判定が壊れ、バイラテラルブラーが
        // 「エッジ保存」ではなく「ほぼ全サンプル棄却」になってノイズが残る。
        const ICamera* camera = context.sceneManager
            ? context.sceneManager->GetGameViewCamera3D()
            : nullptr;
        if (!camera && context.cameraManager) {
            camera = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        }
        if (camera) {
            const Matrix4x4& view = camera->GetViewMatrix();
            const Matrix4x4& proj = camera->GetProjectionMatrix();
            const Matrix4x4 invViewProj = MathCore::Matrix::Inverse(MathCore::Matrix::Multiply(view, proj));
            std::memcpy(params_.invViewProjMatrix, &invViewProj, sizeof(float) * 16);
        }

        // 今フレームのスライスへ書き込む（フレームオーバーラップ対応）
        const D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
            cbRing_.Upload(context.dxCommon, &params_, sizeof(params_));

        // 入力（生 SSAO またはテンポラル蓄積結果）と出力のレンダーターゲットを取得
        const std::string inputName = inputTargetName_.empty()
            ? std::string(RenderTargetNames::SSAOBuffer)
            : inputTargetName_;
        auto* ssaoTarget = renderTargetManager->GetRenderTarget(inputName);
        auto* ssaoBlurTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SSAOBlurBuffer);

        if (!ssaoTarget || !ssaoBlurTarget) {
            outputSrvHandle = {};
            return;
        }

        // SSAOブラー描画
        ssaoBlurTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // t0: SSAO結果
        const int ssaoIdx = GetRootParamIndex("gTexture");
        if (ssaoIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(ssaoIdx, ssaoTarget->GetSRVHandle());
        }

        // t1: SceneDepth（WorldPosition ターゲット廃止に伴い、深度から復元する）
        const int depthIdx = GetRootParamIndex("gSceneDepth");
        if (depthIdx >= 0 && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(depthIdx, depthHandle);
            }
        }

        // CBV: SSAOBlurParams
        const int paramsIdx = GetRootParamIndex("SSAOBlurParams");
        if (paramsIdx >= 0 && cbAddress != 0) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, cbAddress);
        }

        DrawFullscreenQuad(cmdList);

        ssaoBlurTarget->End(cmdList);

        // 出力SRVハンドルを設定
        outputSrvHandle = ssaoBlurTarget->GetSRVHandle();
    }

    void SSAOBlurTechnique::OnResize(uint32_t width, uint32_t height)
    {
        // 定数は毎フレーム Execute でリングへ書かれるため、CPU 側の値だけ更新すればよい
        params_.screenWidth = static_cast<float>(width);
        params_.screenHeight = static_cast<float>(height);
    }

    const std::wstring& SSAOBlurTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAOBlur.PS.hlsl";
        return path;
    }
}
