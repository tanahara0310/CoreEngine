#include "pch.h"
#include "Graphics/Pipeline/PipelineCache.h"

#include "Utility/Logger/Logger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace CoreEngine
{
    namespace
    {
        /// @brief PSO の中身から名前を作るためのハッシュ（FNV-1a 64bit）
        class Hasher {
        public:
            void Add(const void* data, size_t size)
            {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i) {
                    hash_ ^= bytes[i];
                    hash_ *= 1099511628211ull;
                }
            }

            template <class T>
            void AddValue(const T& value) { Add(&value, sizeof(value)); }

            void AddShader(const D3D12_SHADER_BYTECODE& shader)
            {
                AddValue(shader.BytecodeLength);
                if (shader.pShaderBytecode && shader.BytecodeLength > 0) {
                    Add(shader.pShaderBytecode, shader.BytecodeLength);
                }
            }

            void AddRootSignature(ID3D12RootSignature* rootSignature)
            {
                uint64_t rootHash = 0;
                if (rootSignature) {
                    UINT size = sizeof(rootHash);
                    if (FAILED(rootSignature->GetPrivateData(kRootSignatureHashGuid, &size, &rootHash))) {
                        rootHash = 0;
                    }
                }
                AddValue(rootHash);
            }

            uint64_t Value() const { return hash_; }

        private:
            uint64_t hash_ = 1469598103934665603ull;
        };

        std::wstring MakeName(wchar_t kind, uint64_t hash)
        {
            wchar_t buffer[24];
            std::swprintf(buffer, std::size(buffer), L"%c%016llx", kind, static_cast<unsigned long long>(hash));
            return buffer;
        }

        std::wstring ComputeName(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
        {
            Hasher hasher;
            hasher.AddRootSignature(desc.pRootSignature);
            hasher.AddShader(desc.CS);
            hasher.AddValue(desc.NodeMask);
            hasher.AddValue(desc.Flags);
            return MakeName(L'C', hasher.Value());
        }

        std::wstring GraphicsName(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
        {
            Hasher hasher;
            hasher.AddRootSignature(desc.pRootSignature);
            hasher.AddShader(desc.VS);
            hasher.AddShader(desc.PS);
            hasher.AddShader(desc.DS);
            hasher.AddShader(desc.HS);
            hasher.AddShader(desc.GS);
            hasher.AddValue(desc.StreamOutput.NumEntries);

            // 値は 1 つずつ足す（構造体の詰め物は読まない）
            const D3D12_BLEND_DESC& blend = desc.BlendState;
            hasher.AddValue(blend.AlphaToCoverageEnable);
            hasher.AddValue(blend.IndependentBlendEnable);
            for (const D3D12_RENDER_TARGET_BLEND_DESC& target : blend.RenderTarget) {
                hasher.AddValue(target.BlendEnable);
                hasher.AddValue(target.LogicOpEnable);
                hasher.AddValue(target.SrcBlend);
                hasher.AddValue(target.DestBlend);
                hasher.AddValue(target.BlendOp);
                hasher.AddValue(target.SrcBlendAlpha);
                hasher.AddValue(target.DestBlendAlpha);
                hasher.AddValue(target.BlendOpAlpha);
                hasher.AddValue(target.LogicOp);
                hasher.AddValue(target.RenderTargetWriteMask);
            }
            hasher.AddValue(desc.SampleMask);

            const D3D12_RASTERIZER_DESC& raster = desc.RasterizerState;
            hasher.AddValue(raster.FillMode);
            hasher.AddValue(raster.CullMode);
            hasher.AddValue(raster.FrontCounterClockwise);
            hasher.AddValue(raster.DepthBias);
            hasher.AddValue(raster.DepthBiasClamp);
            hasher.AddValue(raster.SlopeScaledDepthBias);
            hasher.AddValue(raster.DepthClipEnable);
            hasher.AddValue(raster.MultisampleEnable);
            hasher.AddValue(raster.AntialiasedLineEnable);
            hasher.AddValue(raster.ForcedSampleCount);
            hasher.AddValue(raster.ConservativeRaster);

            const D3D12_DEPTH_STENCIL_DESC& depth = desc.DepthStencilState;
            hasher.AddValue(depth.DepthEnable);
            hasher.AddValue(depth.DepthWriteMask);
            hasher.AddValue(depth.DepthFunc);
            hasher.AddValue(depth.StencilEnable);
            hasher.AddValue(depth.StencilReadMask);
            hasher.AddValue(depth.StencilWriteMask);
            for (const D3D12_DEPTH_STENCILOP_DESC* face : { &depth.FrontFace, &depth.BackFace }) {
                hasher.AddValue(face->StencilFailOp);
                hasher.AddValue(face->StencilDepthFailOp);
                hasher.AddValue(face->StencilPassOp);
                hasher.AddValue(face->StencilFunc);
            }

            for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) {
                const D3D12_INPUT_ELEMENT_DESC& element = desc.InputLayout.pInputElementDescs[i];
                if (element.SemanticName) {
                    hasher.Add(element.SemanticName, std::strlen(element.SemanticName));
                }
                hasher.AddValue(element.SemanticIndex);
                hasher.AddValue(element.Format);
                hasher.AddValue(element.InputSlot);
                hasher.AddValue(element.AlignedByteOffset);
                hasher.AddValue(element.InputSlotClass);
                hasher.AddValue(element.InstanceDataStepRate);
            }
            hasher.AddValue(desc.InputLayout.NumElements);
            hasher.AddValue(desc.IBStripCutValue);
            hasher.AddValue(desc.PrimitiveTopologyType);
            hasher.AddValue(desc.NumRenderTargets);
            for (DXGI_FORMAT format : desc.RTVFormats) {
                hasher.AddValue(format);
            }
            hasher.AddValue(desc.DSVFormat);
            hasher.AddValue(desc.SampleDesc.Count);
            hasher.AddValue(desc.SampleDesc.Quality);
            hasher.AddValue(desc.NodeMask);
            hasher.AddValue(desc.Flags);
            return MakeName(L'G', hasher.Value());
        }
    }

    PipelineCache& PipelineCache::Get()
    {
        static PipelineCache instance;
        return instance;
    }

    void PipelineCache::Initialize(ID3D12Device* device, const std::filesystem::path& file)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Logger& log = Logger::GetInstance();
        file_ = file;
        dirty_ = false;
        stale_ = false;
        hits_ = 0;
        misses_ = 0;
        hitSeconds_ = 0.0;
        missSeconds_ = 0.0;
        library_.Reset();
        blob_.clear();

        if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&device_)))) {
            log.Warnf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "PipelineCache: ID3D12Device1 が無いので、PSO をキャッシュしません");
            return;
        }

        std::error_code ec;
        if (std::filesystem::is_regular_file(file_, ec)) {
            std::ifstream in(file_, std::ios::binary);
            blob_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (!blob_.empty()) {
            const HRESULT hr = device_->CreatePipelineLibrary(blob_.data(), blob_.size(), IID_PPV_ARGS(&library_));
            if (FAILED(hr)) {
                // 読めないファイル（ドライバやアダプタが変わったもの）は捨て、空から作り直して書き直す
                log.Infof(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "PipelineCache: {} を読めないので作り直します（HRESULT={:#010x}）",
                    log.PathToUtf8(file_), static_cast<uint32_t>(hr));
                library_.Reset();
                blob_.clear();
                dirty_ = true;
            }
        }
        if (!library_) {
            CreateEmptyLibrary();
        }
    }

    bool PipelineCache::CreateEmptyLibrary()
    {
        if (!device_) {
            return false;
        }
        const HRESULT hr = device_->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library_));
        if (FAILED(hr)) {
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "PipelineCache: PSO のライブラリを作れないので、キャッシュしません（HRESULT={:#010x}）",
                static_cast<uint32_t>(hr));
            library_.Reset();
            return false;
        }
        return true;
    }

    void PipelineCache::Shutdown()
    {
        Save();
        // ライブラリから引いた PSO が残っていてもよいよう、読んだ中身（blob_）はプロセスの終わりまで持つ
        std::lock_guard<std::mutex> lock(mutex_);
        library_.Reset();
        device_.Reset();
    }

    void PipelineCache::LogStats()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "PipelineCache: 引けた {} 個（{:.1f}ms）/ 作った {} 個（{:.1f}ms）",
            hits_, hitSeconds_ * 1000.0, misses_, missSeconds_ * 1000.0);
    }

    bool PipelineCache::Save()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Logger& log = Logger::GetInstance();
        if (!library_ || !dirty_ || file_.empty()) {
            return false;
        }
        dirty_ = false;

        std::error_code ec;
        if (stale_) {
            // 同じ名前で中身の違うものがあったときは、ファイルを消して次の起動で作り直す
            std::filesystem::remove(file_, ec);
            log.Infof(LogCategory::Graphics, LogSubCategory::Pipeline,
                "PipelineCache: 中身の違う PSO があったので {} を消しました", log.PathToUtf8(file_));
            return false;
        }

        const SIZE_T size = library_->GetSerializedSize();
        std::vector<uint8_t> data(size);
        if (size == 0 || FAILED(library_->Serialize(data.data(), size))) {
            return false;
        }

        std::filesystem::create_directories(file_.parent_path(), ec);
        std::filesystem::path temp = file_;
        temp += L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out) {
                return false;
            }
        }
        std::filesystem::rename(temp, file_, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            return false;
        }
        log.Infof(LogCategory::Graphics, LogSubCategory::Pipeline,
            "PipelineCache: {} KB を {} に書きました（引けた {} / 作った {}）",
            size / 1024, log.PathToUtf8(file_), hits_, misses_);
        return true;
    }

    template <class Desc, class LoadFn, class CreateFn>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::LoadOrCreate(
        const std::wstring& name, const Desc& desc, LoadFn&& load, CreateFn&& create, HRESULT* outResult)
    {
        using Clock = std::chrono::steady_clock;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const Clock::time_point loadStart = Clock::now();
            if (library_ && SUCCEEDED(load(library_.Get(), name.c_str(), desc, pso))) {
                ++hits_;
                hitSeconds_ += std::chrono::duration<double>(Clock::now() - loadStart).count();
                if (outResult) {
                    *outResult = S_OK;
                }
                return pso;
            }
        }

        const Clock::time_point createStart = Clock::now();
        const HRESULT hr = create(desc, pso);
        const double createSeconds = std::chrono::duration<double>(Clock::now() - createStart).count();
        if (outResult) {
            *outResult = hr;
        }
        if (FAILED(hr)) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ++misses_;
        missSeconds_ += createSeconds;
        if (library_) {
            const HRESULT stored = library_->StorePipeline(name.c_str(), pso.Get());
            if (SUCCEEDED(stored)) {
                dirty_ = true;
            } else if (stored == E_INVALIDARG) {
                stale_ = true;
                dirty_ = true;
            }
        }
        return pso;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::CreateCompute(
        ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, HRESULT* outResult)
    {
        return LoadOrCreate(ComputeName(desc), desc,
            [](ID3D12PipelineLibrary* library, const wchar_t* name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& d,
                Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
                return library->LoadComputePipeline(name, &d, IID_PPV_ARGS(&out));
            },
            [device](const D3D12_COMPUTE_PIPELINE_STATE_DESC& d, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
                return device->CreateComputePipelineState(&d, IID_PPV_ARGS(&out));
            },
            outResult);
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::CreateGraphics(
        ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, HRESULT* outResult)
    {
        return LoadOrCreate(GraphicsName(desc), desc,
            [](ID3D12PipelineLibrary* library, const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d,
                Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
                return library->LoadGraphicsPipeline(name, &d, IID_PPV_ARGS(&out));
            },
            [device](const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
                return device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out));
            },
            outResult);
    }

    void PipelineCache::TagRootSignature(ID3D12RootSignature* rootSignature, const void* blob, size_t size)
    {
        if (!rootSignature || !blob || size == 0) {
            return;
        }
        Hasher hasher;
        hasher.Add(blob, size);
        const uint64_t hash = hasher.Value();
        rootSignature->SetPrivateData(kRootSignatureHashGuid, sizeof(hash), &hash);
    }
}
