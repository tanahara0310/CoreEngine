#include "pch.h"
#include "RenderTargetManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "RenderTargetNames.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cassert>

#ifdef _DEBUG
#include <Windows.h>
#endif

namespace CoreEngine
{
    RenderTargetManager::~RenderTargetManager()
    {
        Clear();
    }

    std::string RenderTargetManager::MakePostEffectIntermediateTargetName(size_t index)
    {
        return std::string(RenderTargetNames::PostEffectIntermediatePrefix) + std::to_string(index);
    }

    void RenderTargetManager::Initialize(GraphicsCore* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        assert(dxCommon != nullptr && "GraphicsCore must not be null");

        dxCommon_ = dxCommon;
        dsvHeap_ = dsvHeap;
        nextOffscreenIndex_ = 0;

#ifdef _DEBUG
        OutputDebugStringA("[RenderTargetManager] Initialized.\n");
#endif
    }

    RenderTarget* RenderTargetManager::CreateRenderTarget(const RenderTargetDescriptor& desc)
    {
        if (desc.name.empty()) {
#ifdef _DEBUG
            OutputDebugStringA("[RenderTargetManager] ERROR: RenderTarget name is empty.\n");
#endif
            return nullptr;
        }

        // 既に同じ名前のターゲットが存在する場合は警告
        if (HasRenderTarget(desc.name)) {
#ifdef _DEBUG
            std::string msg = "[RenderTargetManager] WARNING: RenderTarget '" + desc.name + "' already exists. Replacing...\n";
            OutputDebugStringA(msg.c_str());
#endif
            RemoveRenderTarget(desc.name);
        }

        // OffscreenRenderTargetを作成
        auto offscreenTarget = std::make_unique<OffscreenRenderTarget>();

        int targetIndex = 0;
        if (!freeOffscreenIndices_.empty()) {
            targetIndex = freeOffscreenIndices_.back();
            freeOffscreenIndices_.pop_back();
        } else {
            targetIndex = nextOffscreenIndex_;
            nextOffscreenIndex_++;
        }
        
        // 初期化
        offscreenTarget->Initialize(dxCommon_, dxCommon_->GetDescriptorAllocator(), desc, targetIndex);

        // ターゲットをマップに登録
        RenderTarget* targetPtr = offscreenTarget.get();
        targets_[desc.name] = std::move(offscreenTarget);

        // 記述子を保存（リサイズ時に使用）
        descriptors_[desc.name] = desc;

#ifdef _DEBUG
        std::string msg = "[RenderTargetManager] Created RenderTarget: " + desc.name + 
                         " (Index: " + std::to_string(targetIndex) + ")\n";
        OutputDebugStringA(msg.c_str());
#endif

        return targetPtr;
    }

    void RenderTargetManager::EnsurePostEffectIntermediateTargets(size_t count)
    {
        for (size_t index = 0; index < count; ++index) {
            const std::string name = MakePostEffectIntermediateTargetName(index);
            if (HasRenderTarget(name)) {
                continue;
            }

            RenderTargetDescriptor desc(name);
            // ポストエフェクトの中間に深度は要らない。既定のままだと 1 枚ごとに
            // 一度も使われない D32 が付いてくる（1080p で 8.3MB/枚）
            desc.needsDepthStencil = false;
            CreateRenderTarget(desc);
        }
    }

    size_t RenderTargetManager::CalcTotalAllocatedBytes() const
    {
        // 使用中のフォーマットだけ分かればよい。未知のものは 4 バイトとみなす
        auto bytesPerPixel = [](DXGI_FORMAT format) -> size_t {
            switch (format) {
            case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
            case DXGI_FORMAT_R32G32_FLOAT:       return 8;
            case DXGI_FORMAT_R11G11B10_FLOAT:    return 4;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_R16G16_FLOAT:
            case DXGI_FORMAT_D32_FLOAT:
            case DXGI_FORMAT_R32_FLOAT:          return 4;
            case DXGI_FORMAT_R8G8_UNORM:         return 2;
            case DXGI_FORMAT_R8_UNORM:           return 1;
            default:                             return 4;
            }
        };

        size_t total = 0;
        for (const auto& [name, target] : targets_) {
            if (!target) {
                continue;
            }
            const size_t pixels = static_cast<size_t>(target->GetWidth()) * static_cast<size_t>(target->GetHeight());
            if (pixels == 0) {
                continue;
            }

            auto descIt = descriptors_.find(name);
            if (descIt == descriptors_.end()) {
                continue; // バックバッファ等、記述子を持たないものは対象外
            }

            total += pixels * bytesPerPixel(descIt->second.format);
            if (descIt->second.needsDepthStencil) {
                total += pixels * bytesPerPixel(descIt->second.depthFormat);
            }
        }
        return total;
    }

    void RenderTargetManager::LogAllocationIfChanged()
    {
        const size_t total = CalcTotalAllocatedBytes();
        if (total == lastLoggedBytes_) {
            return;
        }
        lastLoggedBytes_ = total;

        // ポストエフェクト分（中間・最終・一時）を別建てで出す。
        // 全体には GBuffer や水面の RT も含まれるため、内訳が無いと
        // ポストエフェクト側の最適化が効いたか判定できない
        size_t postEffectBytes = 0;
        size_t postEffectCount = 0;
        std::string postEffectDetail;
        for (const auto& [name, target] : targets_) {
            const bool isPostEffect =
                name.rfind(RenderTargetNames::PostEffectIntermediatePrefix, 0) == 0
                || name == RenderTargetNames::PostEffectFinal
                || name.rfind("PostEffectTransient", 0) == 0;
            if (!isPostEffect || !target) {
                continue;
            }

            auto descIt = descriptors_.find(name);
            const size_t pixels = static_cast<size_t>(target->GetWidth()) * static_cast<size_t>(target->GetHeight());
            const size_t bytes = (descIt != descriptors_.end() && descIt->second.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
                ? pixels * 8 : pixels * 4;
            postEffectBytes += bytes;
            ++postEffectCount;

            if (postEffectDetail.size() < 400) {
                postEffectDetail += " " + name + "(" + std::to_string(target->GetWidth())
                    + "x" + std::to_string(target->GetHeight()) + ")";
            }
        }

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "[RenderTarget] 全体 {} 枚 / {:.1f} MB   うちポストエフェクト {} 枚 / {:.1f} MB",
            targets_.size(), static_cast<double>(total) / (1024.0 * 1024.0),
            postEffectCount, static_cast<double>(postEffectBytes) / (1024.0 * 1024.0));
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "[RenderTarget] ポストエフェクト内訳:{}", postEffectDetail);
    }

    RenderTarget* RenderTargetManager::GetPostEffectIntermediateTarget(size_t index)
    {
        return GetRenderTarget(MakePostEffectIntermediateTargetName(index));
    }

    void RenderTargetManager::EnsurePostEffectFinalTarget()
    {
        if (HasRenderTarget(RenderTargetNames::PostEffectFinal)) {
            return;
        }

        RenderTargetDescriptor desc(RenderTargetNames::PostEffectFinal);
        // 最終出力もバックバッファへ転送するだけなので深度は不要
        desc.needsDepthStencil = false;
        CreateRenderTarget(desc);
    }

    RenderTarget* RenderTargetManager::GetPostEffectFinalTarget()
    {
        return GetRenderTarget(RenderTargetNames::PostEffectFinal);
    }

    RenderTarget* RenderTargetManager::CreateBackBufferTarget(const std::string& name)
    {
        if (HasRenderTarget(name)) {
#ifdef _DEBUG
            std::string msg = "[RenderTargetManager] WARNING: BackBuffer target '" + name + "' already exists. Replacing...\n";
            OutputDebugStringA(msg.c_str());
#endif
            RemoveRenderTarget(name);
        }

        // BackBufferRenderTargetを作成
        auto backBufferTarget = std::make_unique<BackBufferRenderTarget>();
        backBufferTarget->Initialize(dxCommon_);

        // クリアカラーを設定
        float clearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};
        backBufferTarget->SetClearColor(clearColor);

        // ターゲットをマップに登録
        RenderTarget* targetPtr = backBufferTarget.get();
        targets_[name] = std::move(backBufferTarget);

        // バックバッファ用の記述子を作成（リサイズには対応しない）
        RenderTargetDescriptor desc(name);
        desc.autoResize = false;
        descriptors_[name] = desc;

#ifdef _DEBUG
        std::string msg = "[RenderTargetManager] Created BackBuffer target: " + name + "\n";
        OutputDebugStringA(msg.c_str());
#endif

        return targetPtr;
    }

    RenderTarget* RenderTargetManager::GetRenderTarget(const std::string& name)
    {
        auto it = targets_.find(name);
        if (it != targets_.end()) {
            return it->second.get();
        }

#ifdef _DEBUG
        std::string msg = "[RenderTargetManager] WARNING: RenderTarget '" + name + "' not found.\n";
        OutputDebugStringA(msg.c_str());
#endif

        return nullptr;
    }

    const RenderTarget* RenderTargetManager::GetRenderTarget(const std::string& name) const
    {
        auto it = targets_.find(name);
        if (it != targets_.end()) {
            return it->second.get();
        }

        return nullptr;
    }

    bool RenderTargetManager::HasRenderTarget(const std::string& name) const
    {
        return targets_.find(name) != targets_.end();
    }

    void RenderTargetManager::RemoveRenderTarget(const std::string& name)
    {
        auto it = targets_.find(name);
        if (it != targets_.end()) {
            if (auto* offscreenTarget = dynamic_cast<OffscreenRenderTarget*>(it->second.get())) {
                freeOffscreenIndices_.push_back(offscreenTarget->GetIndex());
            }

            targets_.erase(it);
            descriptors_.erase(name);

#ifdef _DEBUG
            std::string msg = "[RenderTargetManager] Removed RenderTarget: " + name + "\n";
            OutputDebugStringA(msg.c_str());
#endif
        }
    }

    void RenderTargetManager::Clear()
    {
        targets_.clear();
        descriptors_.clear();
        nextOffscreenIndex_ = 0;
        freeOffscreenIndices_.clear();

#ifdef _DEBUG
        OutputDebugStringA("[RenderTargetManager] Cleared all render targets.\n");
#endif
    }

    std::vector<std::string> RenderTargetManager::GetRenderTargetNames() const
    {
        std::vector<std::string> names;
        names.reserve(targets_.size());

        for (const auto& [name, _] : targets_) {
            names.push_back(name);
        }

        return names;
    }

    void RenderTargetManager::ResizeAutoTargets(uint32_t newWidth, uint32_t newHeight)
    {
        for (const auto& [name, desc] : descriptors_) {
            if (!desc.autoResize) {
                continue;
            }

            auto it = targets_.find(name);
            if (it == targets_.end()) {
                continue;
            }

            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(it->second.get())) {
                const uint32_t targetWidth = (desc.width > 0)
                    ? desc.width
                    : std::max(1u, static_cast<uint32_t>(newWidth * desc.resolutionScale));
                const uint32_t targetHeight = (desc.height > 0)
                    ? desc.height
                    : std::max(1u, static_cast<uint32_t>(newHeight * desc.resolutionScale));
                offscreen->Resize(targetWidth, targetHeight);
            }
        }
    }
}
