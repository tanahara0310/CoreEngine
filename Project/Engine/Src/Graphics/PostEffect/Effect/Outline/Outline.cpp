#include "pch.h"
#include "Outline.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Camera/View/ViewInfo.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<Vector4> cvOutlineColor{
            "r.Outline.Color", Vector4{ 0.0f, 0.0f, 0.0f, 1.0f },
            "アウトラインの色（RGBA）" };

        CVar<float> cvDepthThreshold{
            "r.Outline.DepthThreshold", 0.5f,
            "エッジと判定する深度差（m）。小さいほど細かい線が出る",
            CVarRange{ 0.01f, 10.0f } };

        CVar<float> cvDepthStrength{
            "r.Outline.DepthStrength", 1.0f,
            "エッジ強度の乗数",
            CVarRange{ 0.1f, 20.0f } };

        CVar<float> cvOutlineWidth{
            "r.Outline.Width", 1.0f,
            "線の太さ（ピクセル）",
            CVarRange{ 1.0f, 4.0f } };

        CVar<bool> cvEnabled{
            "r.Outline.Enabled", false,
            "アウトラインを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Outline";
    }

    void Outline::OnCreateConstantBuffers()
    {
        // アウトラインパラメータ用定数バッファ
        UINT outlineSize = (sizeof(OutlineParams) + 255) & ~255;
        outlineParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), outlineSize);
        [[maybe_unused]] HRESULT hr = outlineParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedOutlineParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        // 画面サイズ用定数バッファ
    }

    void Outline::UpdateConstantBuffer()
    {
        if (!mappedOutlineParams_) {
            return;
        }
        const Vector4& color = cvOutlineColor.Get();
        mappedOutlineParams_->outlineColor[0] = color.x;
        mappedOutlineParams_->outlineColor[1] = color.y;
        mappedOutlineParams_->outlineColor[2] = color.z;
        mappedOutlineParams_->outlineColor[3] = color.w;

        mappedOutlineParams_->depthThreshold = cvDepthThreshold.Get();
        mappedOutlineParams_->depthStrength  = cvDepthStrength.Get();
        mappedOutlineParams_->outlineWidth   = cvOutlineWidth.Get();
        // クリップ距離はカメラから設定される実行時値
        mappedOutlineParams_->nearPlane = nearPlane_;
        mappedOutlineParams_->farPlane  = farPlane_;
    }

    void Outline::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        // 線形深度への復元には描画に使われたカメラと同じ near/far が要る。
        // ビューが未確定のフレームは前回値を維持する（0 で割る事故を避ける）。
        if (ctx.view && ctx.view->isValid) {
            SetCameraClipPlanes(ctx.view->nearZ, ctx.view->farZ);
        }
    }

    void Outline::DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const
    {
        // 深度は Blackboard 経由で受け取る（直接読むと RenderGraph から見えない依存になる）
        out.push_back({ "gDepth", FrameBlackboard::SceneDepth, /*required*/ true });
    }

    void Outline::SetCameraClipPlanes(float nearPlane, float farPlane)
    {
        // near/farが変わった場合のみ更新してGPU転送コストを抑える
        if (nearPlane_ != nearPlane || farPlane_ != farPlane) {
            nearPlane_ = nearPlane;
            farPlane_  = farPlane;
            UpdateConstantBuffer();
        }
    }

    void Outline::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        // シェーダーリソース名からルートパラメータインデックスを取得
        int textureIdx       = GetRootParamIndex("gTexture");
        int depthIdx         = GetRootParamIndex("gDepth");
        int outputIdx        = GetRootParamIndex("gOutput");
        int outlineParamsIdx = GetRootParamIndex("OutlineParams");
        int screenParamsIdx  = GetRootParamIndex("ScreenParams");

        // カラーテクスチャ (t0)
        if (textureIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        }
        // 深度テクスチャ (t1) - DeclareExtraInputs で申告し、パスが解決したもの
        if (depthIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(depthIdx, GetExtraInput("gDepth"));
        }
        // 出力テクスチャ (u0)
        if (outputIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        }
        // アウトラインパラメータ (b0)
        if (outlineParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(outlineParamsIdx, outlineParamsCB_->GetGPUVirtualAddress());
        }
        // 画面サイズパラメータ (b1)
        if (screenParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(screenParamsIdx, GetScreenSizeCbAddress());
        }

        // ディスパッチ（8x8スレッドグループ）
        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Outline::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("OutlineParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        // クリップ距離はカメラが毎フレーム上書きするため、表示のみ
        ImGui::TextDisabled("クリップ距離（カメラから自動設定）: near %.2f / far %.1f",
                            nearPlane_, farPlane_);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Outline::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
