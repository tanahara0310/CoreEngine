#include "pch.h"
#include "Editor/Scene/SceneOverlayOcclusion.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Utility/Logger/Logger.h"

#include <imgui_impl_dx12.h>

#include <d3dcompiler.h>

#include <cstddef>
#include <cstring>
#include <iterator>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief Begin が積み、Setup が読む値
        struct SetupParams
        {
            float displayPosition[2];
            float displaySize[2];
            float imagePosition[2];
            float imageInverseSize[2];
            float nearZ;
            float farZ;
            float orthographic;
            float padding;
        };

        /// @brief ルート定数（シェーダの Params と同じ並び）
        struct Constants
        {
            float projection[4][4];
            float imageRect[4];   ///< xy = 画像の左上、zw = 1 / 画像の大きさ
            float depthParams[4]; ///< x = near、y = far、z = 平行投影なら 1、w = 奥行きの余裕（割合）
        };

        constexpr UINT kConstantCount = sizeof(Constants) / sizeof(float);

        /// 線の奥行きがシーンより奥に出てよい割合（面に乗った線が隠れないように）
        constexpr float kDepthMargin = 0.01f;

        constexpr const char* kVertexShader = R"(
cbuffer Params : register(b0)
{
    float4x4 ProjectionMatrix;
    float4 ImageRect;
    float4 DepthParams;
};
struct VSInput
{
    float2 pos : POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float2 sceneUV : TEXCOORD1;
};
PSInput main(VSInput input)
{
    PSInput output;
    output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));
    output.col = input.col;
    output.uv = input.uv;
    output.sceneUV = (input.pos.xy - ImageRect.xy) * ImageRect.zw;
    return output;
}
)";

        constexpr const char* kDepthPixelShader = R"(
cbuffer Params : register(b0)
{
    float4x4 ProjectionMatrix;
    float4 ImageRect;
    float4 DepthParams;
};
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float2 sceneUV : TEXCOORD1;
};
Texture2D<float> SceneDepth : register(t0);

float LinearDepth(float depth)
{
    if (DepthParams.z > 0.5f) {
        return lerp(DepthParams.x, DepthParams.y, depth);
    }
    return DepthParams.x * DepthParams.y / (DepthParams.y - depth * (DepthParams.y - DepthParams.x));
}

float4 main(PSInput input) : SV_Target
{
    float4 color = input.col;
    if (all(input.sceneUV >= 0.0f) && all(input.sceneUV < 1.0f)) {
        uint width;
        uint height;
        SceneDepth.GetDimensions(width, height);
        const float scene = SceneDepth.Load(int3(input.sceneUV * float2(width, height), 0));
        const float lineDepth = LinearDepth(saturate(input.uv.x));
        if (lineDepth > LinearDepth(scene) * (1.0f + DepthParams.w)) {
            color.a *= input.uv.y;
        }
    }
    return color;
}
)";

        constexpr const char* kPlainPixelShader = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float2 sceneUV : TEXCOORD1;
};
float4 main(PSInput input) : SV_Target
{
    return input.col;
}
)";

        Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* source, const char* profile)
        {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            if (FAILED(D3DCompile(source, std::strlen(source), "SceneOverlayOcclusion", nullptr, nullptr, "main", profile,
                    0, 0, &blob, &errors))) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "[SceneOverlayOcclusion] シェーダをコンパイルできません: {}",
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
                return nullptr;
            }
            return blob;
        }

        Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device)
        {
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            // 1 番は ImGui の描画が描くたびにテクスチャを差す場所（ImGui のルートシグネチャと同じ番号）
            D3D12_ROOT_PARAMETER parameters[2]{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[0].Constants.ShaderRegister = 0;
            parameters[0].Constants.Num32BitValues = kConstantCount;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[1].DescriptorTable.NumDescriptorRanges = 1;
            parameters[1].DescriptorTable.pDescriptorRanges = &range;
            parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.NumParameters = static_cast<UINT>(std::size(parameters));
            desc.pParameters = parameters;
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
            if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))
                || FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                    IID_PPV_ARGS(&rootSignature)))) {
                return nullptr;
            }
            return rootSignature;
        }

        /// @brief ImGui と同じ描き方（アルファブレンド・深度なし）の PSO
        Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipeline(ID3D12Device* device, ID3D12RootSignature* rootSignature,
            DXGI_FORMAT format, ID3DBlob* vertexShader, ID3DBlob* pixelShader)
        {
            static const D3D12_INPUT_ELEMENT_DESC kLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, pos)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, uv)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(ImDrawVert, col)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rootSignature;
            desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
            desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
            desc.InputLayout = { kLayout, static_cast<UINT>(std::size(kLayout)) };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.SampleMask = UINT_MAX;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = format;
            desc.SampleDesc.Count = 1;
            desc.NodeMask = 1;

            D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;

            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;

            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
            if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)))) {
                return nullptr;
            }
            return pipeline;
        }
    }

    SceneOverlayOcclusion& SceneOverlayOcclusion::Get()
    {
        static SceneOverlayOcclusion instance;
        return instance;
    }

    void SceneOverlayOcclusion::Initialize(GraphicsCore& graphics, SceneDepth* sceneDepth)
    {
        Finalize();
        ID3D12Device* const device = graphics.GetDevice();
        const Microsoft::WRL::ComPtr<ID3DBlob> vertexShader = Compile(kVertexShader, "vs_5_0");
        const Microsoft::WRL::ComPtr<ID3DBlob> depthShader = Compile(kDepthPixelShader, "ps_5_0");
        const Microsoft::WRL::ComPtr<ID3DBlob> plainShader = Compile(kPlainPixelShader, "ps_5_0");
        if (!device || !vertexShader || !depthShader || !plainShader) {
            return;
        }
        rootSignature_ = CreateRootSignature(device);
        if (!rootSignature_) {
            return;
        }
        const DXGI_FORMAT format = graphics.GetSwapChain().RTVFormat();
        depthPipeline_ = CreatePipeline(device, rootSignature_.Get(), format, vertexShader.Get(), depthShader.Get());
        plainPipeline_ = CreatePipeline(device, rootSignature_.Get(), format, vertexShader.Get(), plainShader.Get());
        if (!depthPipeline_ || !plainPipeline_) {
            Finalize();
            return;
        }
        graphics_ = &graphics;
        sceneDepth_ = sceneDepth;
    }

    void SceneOverlayOcclusion::Finalize()
    {
        depthPipeline_.Reset();
        plainPipeline_.Reset();
        rootSignature_.Reset();
        graphics_ = nullptr;
        sceneDepth_ = nullptr;
    }

    bool SceneOverlayOcclusion::Begin(ImDrawList& drawList, const ImVec2& imagePosition, const ImVec2& imageSize,
        const Camera& camera)
    {
        if (!graphics_ || !sceneDepth_ || imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
            return false;
        }
        const ImGuiViewport* const viewport = ImGui::GetWindowViewport();
        const CameraParameters parameters = camera.GetParameters();

        SetupParams params{};
        params.displayPosition[0] = viewport->Pos.x;
        params.displayPosition[1] = viewport->Pos.y;
        params.displaySize[0] = viewport->Size.x;
        params.displaySize[1] = viewport->Size.y;
        params.imagePosition[0] = imagePosition.x;
        params.imagePosition[1] = imagePosition.y;
        params.imageInverseSize[0] = 1.0f / imageSize.x;
        params.imageInverseSize[1] = 1.0f / imageSize.y;
        params.nearZ = parameters.nearClip;
        params.farZ = parameters.farClip;
        params.orthographic = camera.IsOrthographic() ? 1.0f : 0.0f;

        drawList.PushTexture(ImTextureRef(static_cast<ImTextureID>(sceneDepth_->GetDepthSRVHandle().ptr)));
        drawList.AddCallback(&SceneOverlayOcclusion::Setup, &params, sizeof(params));
        return true;
    }

    void SceneOverlayOcclusion::End(ImDrawList& drawList)
    {
        drawList.PopTexture();
        drawList.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    void SceneOverlayOcclusion::Setup(const ImDrawList*, const ImDrawCmd* command)
    {
        SceneOverlayOcclusion& self = Get();
        const auto* const state = static_cast<const ImGui_ImplDX12_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
        if (!state || !state->CommandList || !command->UserCallbackData || !self.rootSignature_) {
            return;
        }
        SetupParams params{};
        std::memcpy(&params, command->UserCallbackData, sizeof(params));

        // 深度を読めるのは、シーンを描いたのと同じコマンドリスト（メインのウィンドウ）で描くときだけ
        ID3D12GraphicsCommandList* const commandList = state->CommandList;
        const bool readDepth = self.graphics_ && self.sceneDepth_ && self.sceneDepth_->GetResource()
            && commandList == self.graphics_->GetCommandList();
        if (readDepth) {
            Barrier::Transition(commandList, self.sceneDepth_->Resource(),
                D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        // ImGui と同じ正射影（表示している範囲の左上から右下へ）
        const float left = params.displayPosition[0];
        const float right = params.displayPosition[0] + params.displaySize[0];
        const float top = params.displayPosition[1];
        const float bottom = params.displayPosition[1] + params.displaySize[1];
        const Constants constants{
            .projection = {
                { 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
                { 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f },
                { 0.0f, 0.0f, 0.5f, 0.0f },
                { (right + left) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1.0f },
            },
            .imageRect = { params.imagePosition[0], params.imagePosition[1], params.imageInverseSize[0], params.imageInverseSize[1] },
            .depthParams = { params.nearZ, params.farZ, params.orthographic, kDepthMargin },
        };

        commandList->SetGraphicsRootSignature(self.rootSignature_.Get());
        commandList->SetPipelineState(readDepth ? self.depthPipeline_.Get() : self.plainPipeline_.Get());
        commandList->SetGraphicsRoot32BitConstants(0, kConstantCount, &constants, 0);
    }
}

#endif // CORE_EDITOR
