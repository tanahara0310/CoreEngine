#include "pch.h"
#include "ColorLUT.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>


namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.ColorLUT.Enabled", false,
            "3D LUT カラーグレーディングを有効にする（LUT 未ロード時は恒等 = 見た目無変化）",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvBlend{
            "r.ColorLUT.Blend", 1.0f,
            "LUT の適用率。0 で元画像、1 で全適用。ルックの強さ調整に使う",
            CVarRange{ 0.0f, 1.0f } };

        constexpr const char* kCVarPrefix = "r.ColorLUT";
    }

    void ColorLUT::OnCreateConstantBuffers()
    {
        auto* device = directXCommon_->GetDevice();

        {
            const UINT size = (sizeof(ColorLUTParams) + 255) & ~255u;
            colorLutParamsCB_ = ResourceFactory::CreateBufferResource(device, size);
            [[maybe_unused]] HRESULT hr = colorLutParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedColorLutParams_));
            assert(SUCCEEDED(hr));
        }
        {
            const UINT size = (sizeof(FillParams) + 255) & ~255u;
            fillParamsCB_ = ResourceFactory::CreateBufferResource(device, size);
            [[maybe_unused]] HRESULT hr = fillParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedFillParams_));
            assert(SUCCEEDED(hr));
        }

        lutResourcesReady_ = CreateLutResources() && CreateFillPipeline();
        if (!lutResourcesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "ColorLUT: LUT リソースの構築に失敗（LUT グレーディングは無効）");
            return;
        }

        // 起動時は恒等 LUT。有効化しても見た目が変わらないことが経路検証の基準になる
        ResetToIdentity();
    }

    bool ColorLUT::CreateLutResources()
    {
        auto* device = directXCommon_->GetDevice();
        DescriptorManager* descriptorManager = directXCommon_->GetDescriptorManager();
        if (!descriptorManager) {
            return false;
        }

        // ---- LUT 本体（Texture3D・UAV 書き込み対応） ----
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Width = kMaxLutSize;
        desc.Height = kMaxLutSize;
        desc.DepthOrArraySize = static_cast<UINT16>(kMaxLutSize);
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        lutTexture_ = ResourceFactory::CreateTextureResource(
            device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!lutTexture_) {
            return false;
        }
        lutTextureState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = kMaxLutSize;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        descriptorManager->CreateSRV(lutTexture_.Get(), srvDesc, cpuHandle, lutSrvHandle_, "ColorLUT_SRV");
        descriptorManager->CreateUAV(lutTexture_.Get(), uavDesc, cpuHandle, lutUavHandle_, "ColorLUT_UAV");

        // ---- LUT データのアップロードバッファ（StructuredBuffer として CS から読む） ----
        const size_t maxTexels = static_cast<size_t>(kMaxLutSize) * kMaxLutSize * kMaxLutSize;
        lutDataBuffer_ = ResourceFactory::CreateBufferResource(device, maxTexels * sizeof(LutTexel));
        if (!lutDataBuffer_ ||
            FAILED(lutDataBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedLutData_)))) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC bufferSrvDesc{};
        bufferSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        bufferSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        bufferSrvDesc.Buffer.NumElements = static_cast<UINT>(maxTexels);
        bufferSrvDesc.Buffer.StructureByteStride = sizeof(LutTexel);
        descriptorManager->CreateSRV(lutDataBuffer_.Get(), bufferSrvDesc, cpuHandle, lutDataSrvHandle_, "ColorLUT_DataSRV");

        return true;
    }

    bool ColorLUT::CreateFillPipeline()
    {
        auto* device = directXCommon_->GetDevice();

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool built = fillPipeline_.Build(device, shaderCompiler, reflectionBuilder, fillProvider_);
        if (!built || !fillPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics, "ColorLUT: Fill パイプラインの構築に失敗");
            return false;
        }
        return true;
    }

    void ColorLUT::BuildPasses(PostEffectGraphBuilder& builder)
    {
        // リソースが無いときはパスを積まない（前段の出力がそのまま次段へ流れる）
        if (!lutResourcesReady_) {
            return;
        }
        PostEffectComputeBase::BuildPasses(builder);
    }

    void ColorLUT::ResetToIdentity()
    {
        if (!mappedLutData_) {
            return;
        }
        constexpr uint32_t n = 33; // 恒等 LUT の代表サイズ（.cube の一般値に合わせる）
        const float scale = 1.0f / static_cast<float>(n - 1);
        for (uint32_t z = 0; z < n; ++z) {
            for (uint32_t y = 0; y < n; ++y) {
                for (uint32_t x = 0; x < n; ++x) {
                    LutTexel& t = mappedLutData_[x + y * n + z * n * n];
                    t.r = x * scale;
                    t.g = y * scale;
                    t.b = z * scale;
                    t.a = 1.0f;
                }
            }
        }
        lutSizeLoaded_ = n;
        lutDirty_ = true;
        loadedLutName_ = "Identity";
        lastError_.clear();
    }

    bool ColorLUT::LoadCubeFile(const std::string& pathOrName)
    {
        if (!mappedLutData_) {
            lastError_ = "LUT リソースが未初期化です";
            return false;
        }

        // フルパス優先。見つからなければアセット名として AssetDatabase で解決を試みる
        // 入力・エラーメッセージは UTF-8 のテキスト、ファイルアクセスは path で扱う。
        Logger& log = Logger::GetInstance();
        std::filesystem::path path = log.Utf8ToPath(pathOrName);
        if (!std::filesystem::exists(path)) {
            path = AssetDatabase::GetInstance().FindAssetPath(log.PathToUtf8(path.filename()));
            if (path.empty() || !std::filesystem::exists(path)) {
                lastError_ = "ファイルが見つかりません: " + pathOrName;
                return false;
            }
        }

        // path を渡すと ifstream はワイド API で開くので、非 ASCII のパスでも通る。
        std::ifstream file(path);
        if (!file) {
            lastError_ = "ファイルを開けません: " + log.PathToUtf8(path);
            return false;
        }

        uint32_t size = 0;
        std::vector<LutTexel> data;
        std::string line;
        while (std::getline(file, line)) {
            // 先頭の空白を落とす
            const size_t begin = line.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                continue;
            }
            line = line.substr(begin);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream iss(line);
            std::string token;
            iss >> token;

            if (token == "TITLE" || token == "DOMAIN_MIN" || token == "DOMAIN_MAX") {
                continue; // DOMAIN は [0,1] 前提。それ以外の .cube は現状想定しない
            }
            if (token == "LUT_1D_SIZE") {
                lastError_ = "1D LUT は未対応です（3D LUT の .cube を指定してください）";
                return false;
            }
            if (token == "LUT_3D_SIZE") {
                int parsed = 0;
                iss >> parsed;
                if (parsed < 2 || parsed > static_cast<int>(kMaxLutSize)) {
                    lastError_ = "対応外の LUT サイズです（2〜64）: " + std::to_string(parsed);
                    return false;
                }
                size = static_cast<uint32_t>(parsed);
                data.reserve(static_cast<size_t>(size) * size * size);
                continue;
            }

            // データ行（r g b）。token は最初の数値
            LutTexel texel;
            try {
                texel.r = std::stof(token);
            } catch (...) {
                continue; // 未知のキーワード行は無視する
            }
            iss >> texel.g >> texel.b;
            if (iss.fail()) {
                lastError_ = "データ行の解析に失敗しました: " + line;
                return false;
            }
            data.push_back(texel);
        }

        const size_t expected = static_cast<size_t>(size) * size * size;
        if (size == 0 || data.size() != expected) {
            lastError_ = "LUT_3D_SIZE とデータ数が一致しません（size=" + std::to_string(size) +
                         " / data=" + std::to_string(data.size()) + "）";
            return false;
        }

        std::copy(data.begin(), data.end(), mappedLutData_);
        lutSizeLoaded_ = size;
        lutDirty_ = true;
        loadedLutName_ = std::filesystem::path(path).filename().string();
        lastError_.clear();
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "ColorLUT: {} を読み込みました（{}^3）", loadedLutName_, size);
        return true;
    }

    void ColorLUT::RecordFillIfDirty(ID3D12GraphicsCommandList* cmdList)
    {
        if (!lutDirty_ || !lutResourcesReady_) {
            return;
        }

        mappedFillParams_->lutSize = lutSizeLoaded_;

        // SRV 状態のままなら UAV へ戻す（初回は生成時から UAV）
        if (lutTextureState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = lutTexture_.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = lutTextureState_;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            cmdList->ResourceBarrier(1, &barrier);
            lutTextureState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        cmdList->SetComputeRootSignature(fillPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(fillPipeline_.GetComputePSO());

        const int dataIdx   = fillPipeline_.GetComputeRootParamIndex("gLutData");
        const int outputIdx = fillPipeline_.GetComputeRootParamIndex("gLutTexture");
        const int paramsIdx = fillPipeline_.GetComputeRootParamIndex("FillParams");
        if (dataIdx >= 0)   cmdList->SetComputeRootDescriptorTable(dataIdx, lutDataSrvHandle_);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, lutUavHandle_);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, fillParamsCB_->GetGPUVirtualAddress());

        const uint32_t groups = (lutSizeLoaded_ + 3) / 4;
        cmdList->Dispatch(groups, groups, groups);

        // 書き込み完了 → SRV で読める状態へ
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = lutTexture_.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        D3D12_RESOURCE_BARRIER toSrv{};
        toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrv.Transition.pResource = lutTexture_.Get();
        toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &toSrv);
        lutTextureState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        lutDirty_ = false;
    }

    void ColorLUT::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        if (!lutResourcesReady_ || !mappedColorLutParams_) {
            return;
        }

        mappedColorLutParams_->screenWidth  = width;
        mappedColorLutParams_->screenHeight = height;
        mappedColorLutParams_->lutSize      = lutSizeLoaded_;
        mappedColorLutParams_->blend        = cvBlend.Get();

        auto* cmdList = directXCommon_->GetCommandList();

        // LUT の差し替え直後だけ Texture3D への書き込みが走る
        RecordFillIfDirty(cmdList);

        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx = GetRootParamIndex("gTexture");
        const int lutIdx     = GetRootParamIndex("gLut");
        const int outputIdx  = GetRootParamIndex("gOutput");
        const int paramsIdx  = GetRootParamIndex("ColorLUTParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (lutIdx >= 0)     cmdList->SetComputeRootDescriptorTable(lutIdx, lutSrvHandle_);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, colorLutParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    }

    void ColorLUT::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ColorLUTParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("LUT: %s（%u^3）", loadedLutName_.c_str(), lutSizeLoaded_);
        UI::Separator();

        ImGui::InputText(".cube パス", pathInputBuffer_, sizeof(pathInputBuffer_));
        if (ImGui::Button("読み込む")) {
            LoadCubeFile(pathInputBuffer_);
        }
        ImGui::SameLine();
        if (ImGui::Button("恒等に戻す")) {
            ResetToIdentity();
        }
        if (!lastError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError_.c_str());
        }

        UI::Separator();
        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* ColorLUT::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
