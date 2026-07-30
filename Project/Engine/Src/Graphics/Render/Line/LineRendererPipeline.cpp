#include "pch.h"
#include "LineRendererPipeline.h"
#include "Camera/ICamera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>


namespace CoreEngine
{
    void LineRendererPipeline::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Line/Line.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Line/Line.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "LineRenderer");

        // 新しいAPIでRootSignatureを構築
        RootSignatureConfig config = RootSignatureConfig::Simple();
        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Line Root Signature: " + buildResult.errorMessage);
        }

        bool result = psoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create pipeline state for LineRendererPipeline.");
        }

        // 深度テスト・深度書き込みを切った PSO も用意しておく（オーバーレイ描画用）
        const bool overlayResult = overlayPsoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!overlayResult) {
            throw std::runtime_error("Failed to create overlay pipeline state for LineRendererPipeline.");
        }

        uint32_t bufferSize = sizeof(LineVertex) * kMaxVertexCount;

        // 頂点バッファリソースを生成
        vertexBuffer_ = ResourceFactory::CreateBufferResource(device, bufferSize);

        vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vbView_.SizeInBytes = bufferSize;
        vbView_.StrideInBytes = sizeof(LineVertex);

        vertices_.reserve(kMaxVertexCount);

        wvpBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(Matrix4x4));
        wvpBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    }

    void LineRendererPipeline::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory) {
        dxCommon_ = dxCommon;
        resourceFactory_ = resourceFactory;
        Initialize(dxCommon->GetDevice());
    }

    void LineRendererPipeline::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        currentCmdList_ = cmdList;
        currentBlendMode_ = blendMode;

        // パイプラインステートを取得
        pipelineState_ = psoMg_->GetPipelineState(blendMode);
        if (!pipelineState_) {
            throw std::runtime_error("Pipeline state not found for specified blend mode.");
        }

        // パイプラインステートとルートシグネチャを設定
        cmdList->SetPipelineState(pipelineState_);
        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());

        // プリミティブトポロジーを設定
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

        // バッチはクリアしない（他のパスで追加されたラインを保持）
        // ClearBatch();
    }

    void LineRendererPipeline::EndPass() {
        // バッチに溜まっているラインを描画
        FlushBatch();

        // バッチをクリア（次フレーム用）
        ClearBatch();

        currentCmdList_ = nullptr;
    }

    void LineRendererPipeline::SetCamera(const ICamera* camera) {
        camera_ = camera;
    }

    void LineRendererPipeline::UpdateVertexBuffer(const std::vector<LineVertex>& vertices) {
        if (vertices.empty() || vertices.size() > kMaxVertexCount) {
            return;
        }

        // 頂点データをコピー
        vertices_ = vertices;

        // GPUに転送
        LineVertex* mappedData = nullptr;
        vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        std::memcpy(mappedData, vertices_.data(), sizeof(LineVertex) * vertices_.size());
        vertexBuffer_->Unmap(0, nullptr);
    }

    int LineRendererPipeline::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void LineRendererPipeline::DrawLines(ID3D12GraphicsCommandList* cmdList, uint32_t vertexCount,
        uint32_t startVertexLocation) {
        if (vertexCount == 0 || !currentCmdList_) {
            return;
        }

        // 頂点バッファを設定
        cmdList->IASetVertexBuffers(0, 1, &vbView_);

        // WVP行列を設定（リフレクションから取得したインデックスを使用）
        int cameraIdx = GetRootParamIndex("Camera");
        if (cameraIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(
                cameraIdx,  // シェーダーリフレクションから自動決定
                wvpBuffer_->GetGPUVirtualAddress());
        }
        cmdList->DrawInstanced(vertexCount, 1, startVertexLocation, 0);
    }

    void LineRendererPipeline::SetWVPMatrix(const Matrix4x4& view, const Matrix4x4& proj) {
        if (wvpData_) {
            *wvpData_ = CoreEngine::MathCore::Matrix::Multiply(view, proj);
        }
    }

    void LineRendererPipeline::AddLine(const Line& line) {
        lineBatch_.push_back(line);
    }

    void LineRendererPipeline::AddLines(const std::vector<Line>& lines) {
        lineBatch_.insert(lineBatch_.end(), lines.begin(), lines.end());
    }

    void LineRendererPipeline::FlushBatch() {
        if (lineBatch_.empty() || !camera_) {
            return;
        }

        // コマンドリストがない場合は描画できない（パスが開始されていない）
        if (!currentCmdList_) {
            return;
        }

        // WVP行列を設定（深度あり・なしで共通）
        Matrix4x4 view = camera_->GetViewMatrix();
        Matrix4x4 proj = camera_->GetProjectionMatrix();
        SetWVPMatrix(view, proj);

        // 深度テストの有無で PSO が変わるので 2 グループに分ける。
        // ただし頂点バッファは 1 本しかないため、途中で作り直すと
        // GPU がまだ最初のドローを実行する前に CPU が上書きしてしまう。
        // そこで「深度あり → 深度なし」の順に 1 本の配列へ詰めて 1 回だけ転送し、
        // 開始頂点をずらした 2 回のドローに分ける。
        std::vector<LineVertex> vertices;
        vertices.reserve(lineBatch_.size() * 2);

        auto appendVertices = [&vertices](const Line& line) {
            vertices.push_back({ line.start, line.color, line.alpha });
            vertices.push_back({ line.end, line.color, line.alpha });
        };

        for (const Line& line : lineBatch_) {
            if (line.depthTest) appendVertices(line);
        }
        const uint32_t depthTestedVertexCount = static_cast<uint32_t>(vertices.size());

        for (const Line& line : lineBatch_) {
            if (!line.depthTest) appendVertices(line);
        }
        const uint32_t overlayVertexCount = static_cast<uint32_t>(vertices.size()) - depthTestedVertexCount;

        UpdateVertexBuffer(vertices);

        if (depthTestedVertexCount > 0) {
            currentCmdList_->SetPipelineState(pipelineState_);
            DrawLines(currentCmdList_, depthTestedVertexCount, 0);
        }

        if (overlayVertexCount > 0) {
            if (ID3D12PipelineState* overlayPso = overlayPsoMg_->GetPipelineState(currentBlendMode_)) {
                currentCmdList_->SetPipelineState(overlayPso);
                DrawLines(currentCmdList_, overlayVertexCount, depthTestedVertexCount);

                // 呼び出し元は通常 PSO が設定されている前提でいるため元に戻す
                currentCmdList_->SetPipelineState(pipelineState_);
            }
        }
    }

    void LineRendererPipeline::ClearBatch() {
        lineBatch_.clear();
    }
}
