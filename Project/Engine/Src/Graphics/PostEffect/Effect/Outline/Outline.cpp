#include "pch.h"
#include "Outline.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Camera/View/ViewInfo.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Utility/CVar/CVar.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#endif


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

    Outline::OutlineParams Outline::MakeParams() const
    {
        OutlineParams params{};
        const Vector4& color = cvOutlineColor.Get();
        params.outlineColor[0] = color.x;
        params.outlineColor[1] = color.y;
        params.outlineColor[2] = color.z;
        params.outlineColor[3] = color.w;

        params.depthThreshold = cvDepthThreshold.Get();
        params.depthStrength  = cvDepthStrength.Get();
        params.outlineWidth   = cvOutlineWidth.Get();
        // クリップ距離はカメラから設定される実行時値
        params.nearPlane = nearPlane_;
        params.farPlane  = farPlane_;
        return params;
    }

    void Outline::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        // 線形深度への復元には描画に使われたカメラと同じ near/far が要る。
        // ビューが未確定のフレームは前回値を維持する（0 で割る事故を避ける）。
        if (ctx.view && ctx.view->isValid) {
            nearPlane_ = ctx.view->nearZ;
            farPlane_  = ctx.view->farZ;
        }
    }

    void Outline::DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const
    {
        // 深度は Blackboard 経由で受け取る（直接読むと RenderGraph から見えない依存になる）
        out.push_back({ "gDepth", FrameBlackboard::SceneDepth, /*required*/ true });
    }

    void Outline::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
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
            cmdList->SetComputeRootConstantBufferView(outlineParamsIdx, UploadConstants(MakeParams()));
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
#ifdef CORE_EDITOR
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
#endif // CORE_EDITOR
    }

    CVar<bool>* Outline::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
