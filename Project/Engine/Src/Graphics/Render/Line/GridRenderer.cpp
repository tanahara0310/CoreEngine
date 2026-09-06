#include "pch.h"
#include "GridRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/Camera.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

#ifdef USE_IMGUI
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif


namespace CoreEngine
{
namespace {
#ifdef USE_IMGUI
    /// 設定パネルの編集対象（シーンの寿命に縛られるポインタをラムダに持たせないための
    /// ファイルスコープ変数。CollisionMatrixPanel と同じ流儀）
    GridRenderer* s_activeGrid = nullptr;
#endif
}

void GridRenderer::Initialize(ID3D12Device* device)
{
    shaderCompiler_->Initialize();
    reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());

    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Grid/Grid.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Grid/Grid.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "GridRenderer");

    RootSignatureConfig config;
    const auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
    if (!buildResult.success) {
        throw std::runtime_error("Failed to create Grid Root Signature: " + buildResult.errorMessage);
    }

    // 頂点バッファは使わない（VS が SV_VertexID から三角形を組み立てる）ので入力レイアウトは空になる。
    // 深度は PS が SV_Depth で床平面の値を出すためテストは行うが、書き込みはしない
    //（半透明の補助表示なので、後ろのパスを遮ってはいけない）。
    const bool result = psoMg_->CreateBuilder()
        .SetDebugName("Grid")
        .SetInputLayoutFromReflection(*reflectionData_)
        .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL)
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .Build(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature(),
               { BlendMode::kBlendModeNormal });

    if (!result) {
        throw std::runtime_error("Failed to create pipeline state for GridRenderer.");
    }

    // グリッドは通常アルファブレンド固定なので、PSO はここで確定させておく
    pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
}

void GridRenderer::Initialize(GraphicsCore* dxCommon)
{
    dxCommon_ = dxCommon;
    Initialize(dxCommon->GetDevice());
}

void GridRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode)
{
    (void)blendMode; // グリッドは通常アルファブレンド固定
    currentCmdList_ = cmdList;

    if (!cmdList || !pipelineState_) {
        return;
    }

    cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
    cmdList->SetPipelineState(pipelineState_);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void GridRenderer::EndPass()
{
    // 描画対象を持たないパスなので、Line パスと同じくフラッシュ相当をここで行う
    DrawGrid();
    currentCmdList_ = nullptr;
}

void GridRenderer::SetCamera(const Camera* camera)
{
    camera_ = camera;
}

void GridRenderer::SetBaseSpacing(float spacing)
{
    baseSpacing_ = std::max(spacing, 1e-3f);
}

void GridRenderer::SetMinPixelsPerCell(float pixels)
{
    // これを小さくしすぎると、段が繰り上がる前に格子が数ピクセルまで詰まって縞になる
    minPixelsPerCell_ = std::max(pixels, 4.0f);
}

int GridRenderer::GetRootParamIndex(const std::string& resourceName) const
{
    if (!reflectionData_) {
        return -1;
    }
    return reflectionData_->GetRootParameterIndexByName(resourceName);
}

void GridRenderer::DrawGrid()
{
    if (!visible_ || !currentCmdList_ || !camera_ || !dxCommon_ || !pipelineState_) {
        return;
    }

    const int paramIndex = GetRootParamIndex("GridParams");
    if (paramIndex < 0) {
        return;
    }

    // ジッタ込みの射影を使う（シーンと同じ行列でないと TAA の履歴とずれる）
    const Matrix4x4 viewProjection = camera_->GetViewMatrix() * camera_->GetProjectionMatrix();
    const Vector3 cameraPosition = camera_->GetPosition();

    // シェーダーはカメラを原点とした座標系で解く。絶対ワールド座標のままだと、カメラが
    // 原点から離れたときに float32 の桁が足りず、交点とその微分がピクセル単位でばらついて
    // 格子も軸ラインも一面のノイズになる。
    // 行ベクトル規約なので、カメラ相対位置 p に対して (p + camera) * VP = p * (T(camera) * VP)。
    // 逆側は、同次座標のまま平行移動を後ろへ掛ければ「逆射影してからカメラを引く」になる。
    GridConstants constants{};
    constants.viewProjection = MathCore::Matrix::Translation(cameraPosition) * viewProjection;
    constants.invViewProjection =
        MathCore::Matrix::Inverse(viewProjection) * MathCore::Matrix::Translation(-cameraPosition);
    constants.cameraPosition = cameraPosition;
    constants.planeY = kGridPlaneYOffset;
    constants.gridColor = normalColor_;
    constants.baseSpacing = baseSpacing_;
    constants.xAxisColor = xAxisColor_;
    constants.brightness = brightness_;
    constants.zAxisColor = zAxisColor_;
    constants.minPixelsPerCell = minPixelsPerCell_;
    constants.maxLevel = kMaxLevel;
    constants.lineWidthPixels = lineWidthPixels_;
    constants.axisAlpha = kAxisAlpha;
    constants.depthBias = kDepthBias;

    // Line パスと同様、この実行専用の領域を取る（1 本を使い回すとビューごとに上書きされる）
    const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->GetUploadRing().AllocateConstants(constants);
    if (address == 0) {
        return;
    }

    currentCmdList_->SetGraphicsRootConstantBufferView(static_cast<UINT>(paramIndex), address);
    // VS が SV_VertexID から画面全体を覆う三角形を作るので、頂点バッファの設定は要らない
    currentCmdList_->DrawInstanced(3, 1, 0, 0);
}

void GridRenderer::SubmitLines(LineRendererPipeline& pipeline, const Camera* camera)
{
    if (!visible_ || !camera) {
        return;
    }

    const float alpha = kAxisAlpha * brightness_;
    if (alpha <= 0.0f) {
        return;
    }

    // 垂直な Y 軸だけは床平面に乗らないので解析グリッドでは描けない。原点の目印として
    // Line パスへ 1 本だけ流す。長さをカメラ高度に比例させることで、どの高さでも
    // 画面上の見え方が変わらない（旧実装の「最細の段に合わせる」と同じ狙い）。
    const float height = std::max(std::abs(camera->GetPosition().y - kGridPlaneYOffset), baseSpacing_);
    const float halfHeight = height * kYAxisHeightScale;

    pipeline.AddLine(Line{ { 0.0f, kGridPlaneYOffset - halfHeight, 0.0f },
                           { 0.0f, kGridPlaneYOffset + halfHeight, 0.0f },
                           yAxisColor_, alpha });
}

#ifdef USE_IMGUI
void GridRenderer::EnsureSettingsPanelRegistered(EngineSystem* engine)
{
    static bool registered = false;
    if (registered || !engine) {
        return;
    }

    auto* debug = engine->GetDebugSubsystem();
    auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
    if (!gameDebugUI) {
        return;
    }

    // ドロワーは何もキャプチャしない（ファイルスコープの s_activeGrid を読むだけ）
    gameDebugUI->RegisterEnginePanel("Grid", [] {
        if (s_activeGrid) {
            s_activeGrid->DrawSettingsImGui();
        } else {
            ImGui::TextDisabled("(グリッドがありません)");
        }
    });

    registered = true;
}

void GridRenderer::SetActiveForSettingsPanel(GridRenderer* grid)
{
    s_activeGrid = grid;
}

bool GridRenderer::DrawSettingsImGui()
{
    bool changed = false;

    UI::SectionHeader("表示");
    if (UI::Widgets::ToggleSwitch("表示", &visible_)) {
        changed = true;
    }

    UI::SectionHeader("スケール");
    ImGui::TextDisabled("1 マスが画面上でこの px 数を下回ると 10 倍粗い格子へ切り替わります");
    if (UI::DragFloat("最細の間隔 [m]", baseSpacing_, 0.05f, 0.01f, 100.0f)) {
        baseSpacing_ = std::max(baseSpacing_, 1e-3f);
        changed = true;
    }
    if (UI::DragFloat("1 マスの最小 px", minPixelsPerCell_, 0.5f, 4.0f, 64.0f)) {
        minPixelsPerCell_ = std::max(minPixelsPerCell_, 4.0f);
        changed = true;
    }
    if (UI::DragFloat("線の太さ [px]", lineWidthPixels_, 0.05f, 0.5f, 5.0f)) {
        lineWidthPixels_ = std::max(lineWidthPixels_, 0.1f);
        changed = true;
    }
    if (UI::DragFloat("濃さ", brightness_, 0.01f, 0.0f, 1.0f)) {
        changed = true;
    }

    UI::SectionHeader("カラー");
    if (UI::ColorEdit3("X 軸色", xAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("Y 軸色", yAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("Z 軸色", zAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("グリッド色", normalColor_)) {
        changed = true;
    }

    return changed;
}
#endif // USE_IMGUI
}
