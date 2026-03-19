#include "PipelineStateManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace
{
    // DXGI_FORMATを文字列に変換するヘルパー関数
    std::string FormatToString(DXGI_FORMAT format)
    {
        switch (format) {
            case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
            case DXGI_FORMAT_R32G32B32A32_UINT:  return "R32G32B32A32_UINT";
            case DXGI_FORMAT_R32G32B32A32_SINT:  return "R32G32B32A32_SINT";
            case DXGI_FORMAT_R32G32B32_FLOAT:    return "R32G32B32_FLOAT";
            case DXGI_FORMAT_R32G32B32_UINT:     return "R32G32B32_UINT";
            case DXGI_FORMAT_R32G32B32_SINT:     return "R32G32B32_SINT";
            case DXGI_FORMAT_R32G32_FLOAT:       return "R32G32_FLOAT";
            case DXGI_FORMAT_R32G32_UINT:        return "R32G32_UINT";
            case DXGI_FORMAT_R32G32_SINT:        return "R32G32_SINT";
            case DXGI_FORMAT_R32_FLOAT:          return "R32_FLOAT";
            case DXGI_FORMAT_R32_UINT:           return "R32_UINT";
            case DXGI_FORMAT_R32_SINT:           return "R32_SINT";
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
            case DXGI_FORMAT_R16G16_FLOAT:       return "R16G16_FLOAT";
            case DXGI_FORMAT_R8G8B8A8_UNORM:     return "R8G8B8A8_UNORM";
            case DXGI_FORMAT_R8G8B8A8_UINT:      return "R8G8B8A8_UINT";
            default:                             return "UNKNOWN";
        }
    }
}

// ================================================================================
// PSOビルダークラス
// ================================================================================


namespace CoreEngine
{
PipelineStateBuilder::PipelineStateBuilder(PipelineStateManager* manager)
    : manager_(manager)
    , numRenderTargets_(1)
    , colorWriteMask_(D3D12_COLOR_WRITE_ENABLE_ALL)
    , enableAlphaWrite_(true)
    , depthWriteEnabled_(true)
{
    InitializeDefaults();
}

void PipelineStateBuilder::InitializeDefaults()
{
    // ラスタライザのデフォルト設定
    rasterizerDesc_ = {};
    rasterizerDesc_.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc_.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc_.FrontCounterClockwise = FALSE;
    rasterizerDesc_.DepthBias = 0;
    rasterizerDesc_.DepthBiasClamp = 0.0f;
    rasterizerDesc_.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc_.DepthClipEnable = TRUE;
    rasterizerDesc_.MultisampleEnable = FALSE;
    rasterizerDesc_.AntialiasedLineEnable = FALSE;
    rasterizerDesc_.ForcedSampleCount = 0;
    rasterizerDesc_.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // 深度ステンシルのデフォルト設定
    depthStencilDesc_ = {};
    depthStencilDesc_.DepthEnable = TRUE;
    depthStencilDesc_.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc_.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencilDesc_.StencilEnable = FALSE;

    // プリミティブトポロジ
    primitiveTopologyType_ = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // レンダーターゲットフォーマット
    for (int i = 0; i < 8; ++i) {
        rtvFormats_[i] = DXGI_FORMAT_UNKNOWN;
    }
    rtvFormats_[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // 深度ステンシルフォーマット
    dsvFormat_ = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // サンプル設定
    sampleDesc_.Count = 1;
    sampleDesc_.Quality = 0;
}

PipelineStateBuilder& PipelineStateBuilder::AddInputElement(
    const char* semanticName,
    UINT semanticIndex,
    DXGI_FORMAT format,
    UINT alignedByteOffset,
    UINT inputSlot)
{
    D3D12_INPUT_ELEMENT_DESC elementDesc{};
    elementDesc.SemanticName = semanticName;
    elementDesc.SemanticIndex = semanticIndex;
    elementDesc.Format = format;
    elementDesc.InputSlot = inputSlot;
    elementDesc.AlignedByteOffset = alignedByteOffset;
    elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elementDesc.InstanceDataStepRate = 0;

    inputElementDescs_.push_back(elementDesc);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetInputLayoutFromReflection(const ShaderReflectionData& reflectionData)
{
    // 既存の入力レイアウトをクリア
    inputElementDescs_.clear();
    semanticNameStorage_.clear();
    
    // 自動スロット検出を適用した入力要素を取得
    const auto inputElements = reflectionData.GetInputElementsWithAutoSlots();
    
    // セマンティック名を永続的に保持するためのストレージを確保
    semanticNameStorage_.reserve(inputElements.size());
    
    for (const auto& element : inputElements) {
        // セマンティック名を永続化（D3D12_INPUT_ELEMENT_DESCはポインタを保持するため）
        semanticNameStorage_.push_back(element.semanticName);
        
        D3D12_INPUT_ELEMENT_DESC desc{};
        desc.SemanticName = semanticNameStorage_.back().c_str();
        desc.SemanticIndex = element.semanticIndex;
        desc.Format = element.format;
        desc.InputSlot = element.inputSlot;
        desc.AlignedByteOffset = element.alignedByteOffset;
        desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        desc.InstanceDataStepRate = 0;
        
        inputElementDescs_.push_back(desc);
    }
    
#ifdef _DEBUG
    // ログ出力
    if (!inputElements.empty()) {
        std::ostringstream oss;
        oss << "\n";
        oss << "┌─────────────────────────────────────────────────────────────────┐\n";
        oss << "│" << std::setw(35) << reflectionData.GetShaderName() << " - InputLayout" << std::setw(17) << "│\n";
        oss << "├─────────────────────────────────────────────────────────────────┤\n";
        oss << "│  Applied Input Elements: " << inputElements.size() << std::setw(38 - std::to_string(inputElements.size()).length()) << "│\n";
        oss << "├─────────────────────────────────────────────────────────────────┤\n";
        
        for (const auto& element : inputElements) {
            std::string formatStr = FormatToString(element.format);
            oss << "│    • " << element.semanticName << element.semanticIndex 
                << "  (Slot:" << element.inputSlot << ", " << formatStr << ")" 
                << std::setw(static_cast<int>(55 - element.semanticName.length() - formatStr.length())) << "│\n";
        }
        
        oss << "└─────────────────────────────────────────────────────────────────┘\n";
        
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Shader, "{}", oss.str());
    }
#endif
    
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetRasterizer(D3D12_CULL_MODE cullMode, D3D12_FILL_MODE fillMode)
{
    rasterizerDesc_.CullMode = cullMode;
    rasterizerDesc_.FillMode = fillMode;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetDepthBias(
    INT depthBias,
    float slopeScaledDepthBias,
    float depthBiasClamp)
{
    rasterizerDesc_.DepthBias = depthBias;
    rasterizerDesc_.SlopeScaledDepthBias = slopeScaledDepthBias;
    rasterizerDesc_.DepthBiasClamp = depthBiasClamp;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetDepthStencil(
    bool enableDepth,
    bool enableWrite,
    D3D12_COMPARISON_FUNC depthFunc)
{
    depthStencilDesc_.DepthEnable = enableDepth ? TRUE : FALSE;
    depthStencilDesc_.DepthWriteMask = enableWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc_.DepthFunc = depthFunc;
    depthWriteEnabled_ = enableWrite;
    return *this;
}




PipelineStateBuilder& PipelineStateBuilder::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType)
{
    primitiveTopologyType_ = topologyType;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetRenderTargetFormat(DXGI_FORMAT format, UINT index)
{
    if (index < 8) {
        rtvFormats_[index] = format;

        // DXGI_FORMAT_UNKNOWNの場合、レンダーターゲット数を0にする（深度のみのパス用）
        if (index == 0 && format == DXGI_FORMAT_UNKNOWN) {
            numRenderTargets_ = 0;
        } else if (index >= numRenderTargets_) {
            numRenderTargets_ = index + 1;
        }
    }
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetRenderTargetFormats(const DXGI_FORMAT* formats, UINT count)
{
    assert(formats != nullptr && "SetRenderTargetFormats: formats が nullptr です");
    assert(count > 0 && count <= 8 && "SetRenderTargetFormats: count は 1〜8 の範囲で指定してください");

    // 一度全スロットをリセット
    for (int i = 0; i < 8; ++i) {
        rtvFormats_[i] = DXGI_FORMAT_UNKNOWN;
    }

    for (UINT i = 0; i < count; ++i) {
        rtvFormats_[i] = formats[i];
    }
    numRenderTargets_ = count;

    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetDepthStencilFormat(DXGI_FORMAT format)
{
    dsvFormat_ = format;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetSampleDesc(UINT count, UINT quality)
{
    sampleDesc_.Count = count;
    sampleDesc_.Quality = quality;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::SetColorWriteMask(D3D12_COLOR_WRITE_ENABLE writeMask, bool enableAlpha)
{
    colorWriteMask_ = writeMask;
    enableAlphaWrite_ = enableAlpha;
    return *this;
}

bool PipelineStateBuilder::Build(
    ID3D12Device* device,
    IDxcBlob* vs,
    IDxcBlob* ps,
    ID3D12RootSignature* rootSignature,
    const std::vector<BlendMode>& modes)
{
    std::vector<BlendMode> targetModes = modes;
    if (targetModes.empty()) {
        targetModes.push_back(BlendMode::kBlendModeNone);
    }

    for (BlendMode mode : targetModes) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = CreatePipelineStateDesc(vs, ps, rootSignature, mode);

        ComPtr<ID3D12PipelineState> pipelineState;
        HRESULT result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));

        if (FAILED(result)) {
            return false;
        }

        manager_->RegisterPipelineState(mode, pipelineState);
    }

    return true;
}

bool PipelineStateBuilder::BuildAllBlendModes(
    ID3D12Device* device,
    IDxcBlob* vs,
    IDxcBlob* ps,
    ID3D12RootSignature* rootSignature)
{
#ifdef _DEBUG
    // MRT設定（G-Bufferパス）で BuildAllBlendModes() を呼び出した場合の誤用警告
    // G-BufferパスのPSOは kBlendModeNone のみ必要なため BuildGBuffer() を使用してください
    if (numRenderTargets_ > 1) {
        Logger::GetInstance().Logf(
            LogLevel::Warn,
            LogCategory::Graphics,
            "PipelineStateBuilder::BuildAllBlendModes() が MRT ({} RTs) で呼び出されました。"
            " G-BufferパスのPSOには BuildGBuffer() を使用してください。",
            numRenderTargets_);
    }
#endif

    std::vector<BlendMode> allModes = {
        BlendMode::kBlendModeNone,
        BlendMode::kBlendModeNormal,
        BlendMode::kBlendModeAdd,
        BlendMode::kBlendModeSubtract,
        BlendMode::kBlendModeMultiply,
        BlendMode::kBlendModeScreen
    };

    return Build(device, vs, ps, rootSignature, allModes);
}

bool PipelineStateBuilder::BuildGBuffer(
    ID3D12Device* device,
    IDxcBlob* vs,
    IDxcBlob* ps,
    ID3D12RootSignature* rootSignature)
{
#ifdef _DEBUG
    // G-Buffer は必ず MRT を使用するため単一RTで呼び出した場合は警告
    if (numRenderTargets_ <= 1) {
        Logger::GetInstance().Logf(
            LogLevel::Warn,
            LogCategory::Graphics,
            "PipelineStateBuilder::BuildGBuffer() が単一RT ({} RTs) で呼び出されました。"
            " G-BufferパスではSetRenderTargetFormats()で複数RTを設定してください。",
            numRenderTargets_);
    }
#endif

    // G-Bufferは kBlendModeNone のみ生成する（透過はフォワードパスで行う）
    return Build(device, vs, ps, rootSignature, { BlendMode::kBlendModeNone });
}

D3D12_BLEND_DESC PipelineStateBuilder::CreateBlendDesc(BlendMode mode) const
{
    D3D12_BLEND_DESC desc{};

    // カラーライトマスクの設定
    UINT8 writeMask = static_cast<UINT8>(colorWriteMask_);
    if (!enableAlphaWrite_) {
        writeMask &= ~D3D12_COLOR_WRITE_ENABLE_ALPHA;
    }

    // MRT対応: 全アクティブスロットにライトマスクを設定する
    // G-Bufferパスでは全スロットが BlendEnable = FALSE（ブレンドなし）になる
    // 単一RTの場合は numRenderTargets_ = 1 のため従来と同じ動作
    for (UINT i = 0; i < numRenderTargets_; ++i) {
        desc.RenderTarget[i].RenderTargetWriteMask = writeMask;
    }

    // ブレンド設定は RT[0] にのみ適用する（MRT時もRT[0]のみがブレンド対象）
    switch (mode) {
    case BlendMode::kBlendModeNone:
        desc.RenderTarget[0].BlendEnable = FALSE;
        break;

    case BlendMode::kBlendModeNormal:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case BlendMode::kBlendModeAdd:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case BlendMode::kBlendModeSubtract:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case BlendMode::kBlendModeMultiply:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case BlendMode::kBlendModeScreen:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    }

    return desc;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineStateBuilder::CreatePipelineStateDesc(
    IDxcBlob* vs,
    IDxcBlob* ps,
    ID3D12RootSignature* rootSignature,
    BlendMode mode) const
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature;

    // 入力レイアウト
    if (!inputElementDescs_.empty()) {
        desc.InputLayout.pInputElementDescs = inputElementDescs_.data();
        desc.InputLayout.NumElements = static_cast<UINT>(inputElementDescs_.size());
    }

    // シェーダー
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    
    // ピクセルシェーダーがnullptrの場合は設定しない（シャドウマップ用など）
    if (ps != nullptr) {
        desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    } else {
        desc.PS = { nullptr, 0 };
    }

    // ブレンド設定
    desc.BlendState = CreateBlendDesc(mode);
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // ラスタライザ
    desc.RasterizerState = rasterizerDesc_;

    // 深度ステンシル
    desc.DepthStencilState = depthStencilDesc_;

    // プリミティブトポロジ
    desc.PrimitiveTopologyType = primitiveTopologyType_;

    // レンダーターゲット
    desc.NumRenderTargets = numRenderTargets_;
    for (UINT i = 0; i < numRenderTargets_; ++i) {
        desc.RTVFormats[i] = rtvFormats_[i];
    }

    // 深度ステンシルフォーマット
    desc.DSVFormat = dsvFormat_;

    // サンプル設定
    desc.SampleDesc = sampleDesc_;

    return desc;
}

// ================================================================================
// PSOマネージャークラス
// ================================================================================

ID3D12PipelineState* PipelineStateManager::GetPipelineState(BlendMode mode)
{
    auto it = pipelineStates_.find(mode);
    if (it != pipelineStates_.end()) {
        return it->second.Get();
    }
    return nullptr;
}

PipelineStateBuilder PipelineStateManager::CreateBuilder()
{
    return PipelineStateBuilder(this);
}

void PipelineStateManager::Clear()
{
    pipelineStates_.clear();
}

void PipelineStateManager::RegisterPipelineState(BlendMode mode, ComPtr<ID3D12PipelineState> pso)
{
    pipelineStates_[mode] = pso;
}
}


