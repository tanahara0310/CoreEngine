#include "LineRendererPipeline.h"
#include "Camera/ICamera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
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

        uint32_t bufferSize = sizeof(LineVertex) * kMaxVertexCount;

        D3D12_HEAP_PROPERTIES heapProp = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = bufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer_));

        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create vertex buffer for LineRendererPipeline.");
        }

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

    void LineRendererPipeline::DrawLines(ID3D12GraphicsCommandList* cmdList, uint32_t vertexCount) {
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
        cmdList->DrawInstanced(vertexCount, 1, 0, 0);
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

        // ラインを頂点データに変換
        std::vector<LineVertex> vertices;
        vertices.reserve(lineBatch_.size() * 2);

        for (const auto& line : lineBatch_) {
            vertices.push_back({ line.start, line.color, line.alpha });
            vertices.push_back({ line.end, line.color, line.alpha });
        }

        // 頂点バッファを更新
        UpdateVertexBuffer(vertices);

        // WVP行列を設定
        Matrix4x4 view = camera_->GetViewMatrix();
        Matrix4x4 proj = camera_->GetProjectionMatrix();
        SetWVPMatrix(view, proj);

        // 描画
        DrawLines(currentCmdList_, static_cast<uint32_t>(vertices.size()));
    }

    void LineRendererPipeline::ClearBatch() {
        lineBatch_.clear();
    }
}
