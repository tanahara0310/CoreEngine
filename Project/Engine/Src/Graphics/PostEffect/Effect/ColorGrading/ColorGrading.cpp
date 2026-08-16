#include "pch.h"
#include "ColorGrading.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>
#include <cassert>
#include <cmath>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvHue{
            "r.ColorGrading.Hue", 0.0f,
            "色相の回転量",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> cvSaturation{
            "r.ColorGrading.Saturation", 1.0f,
            "彩度。0 でモノクロ",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvValue{
            "r.ColorGrading.Value", 1.0f,
            "明度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvContrast{
            "r.ColorGrading.Contrast", 1.0f,
            "コントラスト",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvGamma{
            "r.ColorGrading.Gamma", 1.0f,
            "ガンマ補正",
            CVarRange{ 0.1f, 3.0f } };

        CVar<float> cvTemperatureK{
            "r.ColorGrading.TemperatureK", 6504.0f,
            "ホワイトバランス色温度 [K]。6504(D65) で無補正。上げると暖色（夕景の白を白に）、"
            "下げると寒色。Bradford 色順応なので強く振っても色相が破綻しない",
            CVarRange{ 1500.0f, 15000.0f } };

        CVar<float> cvTint{
            "r.ColorGrading.Tint", 0.0f,
            "ティント（マゼンタ⇔グリーン）。白色点を黒体軌跡と直交方向へずらす",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> cvExposure{
            "r.ColorGrading.Exposure", 0.0f,
            "露出調整",
            CVarRange{ -3.0f, 3.0f } };

        CVar<Vector3> cvShadowLift{
            "r.ColorGrading.ShadowLift", Vector3{ 0.0f, 0.0f, 0.0f },
            "暗部の持ち上げ量（RGB 個別）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<Vector3> cvMidtoneGamma{
            "r.ColorGrading.MidtoneGamma", Vector3{ 1.0f, 1.0f, 1.0f },
            "中間調のガンマ（RGB 個別）",
            CVarRange{ 0.1f, 3.0f } };

        CVar<Vector3> cvHighlightGain{
            "r.ColorGrading.HighlightGain", Vector3{ 1.0f, 1.0f, 1.0f },
            "明部のゲイン（RGB 個別）",
            CVarRange{ 0.0f, 3.0f } };

        CVar<bool> cvEnabled{
            "r.ColorGrading.Enabled", false,
            "カラーグレーディングを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.ColorGrading";

        // ===== Bradford 色順応によるホワイトバランス行列の計算 =====
        // 「色温度 K の光で照らされた白が白く見える」よう、W(K) → D65 の順応行列を作る。
        // 写真の慣習と同じで、K を上げる（青い光源を仮定する）と補正は暖色側へ働く。

        struct Mat3 {
            float m[3][3];
        };

        Mat3 Mul(const Mat3& a, const Mat3& b)
        {
            Mat3 r{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k)
                        r.m[i][j] += a.m[i][k] * b.m[k][j];
            return r;
        }

        /// @brief 色温度[K] → 黒体軌跡上の CIE xy 色度（Kim らの近似式。1667K〜25000K）
        void KelvinToXy(float kelvin, float& outX, float& outY)
        {
            const double t = std::clamp(kelvin, 1667.0f, 25000.0f);
            const double t2 = t * t;
            const double t3 = t2 * t;
            double x;
            if (t <= 4000.0) {
                x = -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910;
            } else {
                x = -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
            }
            const double x2 = x * x;
            const double x3 = x2 * x;
            double y;
            if (t <= 2222.0) {
                y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
            } else if (t <= 4000.0) {
                y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
            } else {
                y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
            }
            outX = static_cast<float>(x);
            outY = static_cast<float>(y);
        }

        /// @brief W(kelvin, tint) → D65 の色順応を sRGB 空間で行う 3x3 行列
        Mat3 ComputeWhiteBalanceMatrix(float kelvin, float tint)
        {
            // Bradford 錐体応答行列とその逆
            const Mat3 bradford = { { { 0.8951f, 0.2664f, -0.1614f },
                                      { -0.7502f, 1.7135f, 0.0367f },
                                      { 0.0389f, -0.0685f, 1.0296f } } };
            const Mat3 bradfordInv = { { { 0.9869929f, -0.1470543f, 0.1599627f },
                                         { 0.4323053f, 0.5183603f, 0.0492912f },
                                         { -0.0085287f, 0.0400428f, 0.9684867f } } };
            // sRGB(D65) ⇔ XYZ
            const Mat3 srgbToXyz = { { { 0.4124564f, 0.3575761f, 0.1804375f },
                                       { 0.2126729f, 0.7151522f, 0.0721750f },
                                       { 0.0193339f, 0.1191920f, 0.9503041f } } };
            const Mat3 xyzToSrgb = { { { 3.2404542f, -1.5371385f, -0.4985314f },
                                       { -0.9692660f, 1.8760108f, 0.0415560f },
                                       { 0.0556434f, -0.2040259f, 1.0572252f } } };

            float wx = 0.0f, wy = 0.0f;
            KelvinToXy(kelvin, wx, wy);
            // ティントは黒体軌跡とおおよそ直交する y 方向のずらし
            wy = std::clamp(wy + tint * 0.05f, 0.05f, 0.85f);

            // 順応元 = 指定色温度の白色点、順応先 = D65
            const float srcXyz[3] = { wx / wy, 1.0f, (1.0f - wx - wy) / wy };
            const float dstXyz[3] = { 0.95047f, 1.0f, 1.08883f };

            float srcCone[3]{}, dstCone[3]{};
            for (int i = 0; i < 3; ++i) {
                for (int k = 0; k < 3; ++k) {
                    srcCone[i] += bradford.m[i][k] * srcXyz[k];
                    dstCone[i] += bradford.m[i][k] * dstXyz[k];
                }
            }

            Mat3 scale{};
            for (int i = 0; i < 3; ++i) {
                scale.m[i][i] = dstCone[i] / std::max(srcCone[i], 1e-6f);
            }

            const Mat3 cat = Mul(bradfordInv, Mul(scale, bradford));
            return Mul(xyzToSrgb, Mul(cat, srgbToXyz));
        }
    }

    void ColorGrading::OnCreateConstantBuffers()
    {
        UINT cgSize = (sizeof(ColorGradingParams) + 255) & ~255;
        colorGradingParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), cgSize);
        [[maybe_unused]] HRESULT hr = colorGradingParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedColorGradingParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void ColorGrading::UpdateConstantBuffer()
    {
        if (!mappedColorGradingParams_) {
            return;
        }
        mappedColorGradingParams_->hue        = cvHue.Get();
        mappedColorGradingParams_->saturation = cvSaturation.Get();
        mappedColorGradingParams_->value      = cvValue.Get();
        mappedColorGradingParams_->contrast   = cvContrast.Get();
        mappedColorGradingParams_->gamma      = cvGamma.Get();
        mappedColorGradingParams_->exposure   = cvExposure.Get();

        // WB 行列は Kelvin/Tint が変わったときだけ再計算する（毎フレームの 3x3 積を避ける）
        const float kelvin = cvTemperatureK.Get();
        const float tint = cvTint.Get();
        if (kelvin != lastKelvin_ || tint != lastTint_) {
            lastKelvin_ = kelvin;
            lastTint_ = tint;
            const Mat3 wb = ComputeWhiteBalanceMatrix(kelvin, tint);
            for (int i = 0; i < 3; ++i) {
                mappedColorGradingParams_->whiteBalanceRow0[i] = wb.m[0][i];
                mappedColorGradingParams_->whiteBalanceRow1[i] = wb.m[1][i];
                mappedColorGradingParams_->whiteBalanceRow2[i] = wb.m[2][i];
            }
        }

        const Vector3& shadowLift = cvShadowLift.Get();
        mappedColorGradingParams_->shadowLift[0] = shadowLift.x;
        mappedColorGradingParams_->shadowLift[1] = shadowLift.y;
        mappedColorGradingParams_->shadowLift[2] = shadowLift.z;

        const Vector3& midtoneGamma = cvMidtoneGamma.Get();
        mappedColorGradingParams_->midtoneGamma[0] = midtoneGamma.x;
        mappedColorGradingParams_->midtoneGamma[1] = midtoneGamma.y;
        mappedColorGradingParams_->midtoneGamma[2] = midtoneGamma.z;

        const Vector3& highlightGain = cvHighlightGain.Get();
        mappedColorGradingParams_->highlightGain[0] = highlightGain.x;
        mappedColorGradingParams_->highlightGain[1] = highlightGain.y;
        mappedColorGradingParams_->highlightGain[2] = highlightGain.z;
    }

    void ColorGrading::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int cgIdx      = GetRootParamIndex("ColorGradingParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (cgIdx >= 0)      cmdList->SetComputeRootConstantBufferView(cgIdx, colorGradingParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ColorGrading::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ColorGrading");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* ColorGrading::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
