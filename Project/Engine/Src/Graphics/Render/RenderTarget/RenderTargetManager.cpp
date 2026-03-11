#include "RenderTargetManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>

#ifdef _DEBUG
#include <Windows.h>
#endif

namespace CoreEngine
{
    void RenderTargetManager::Initialize(DirectXCommon* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        assert(dxCommon != nullptr && "DirectXCommon must not be null");

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
        
        // 初期化
        offscreenTarget->Initialize(dxCommon_, nextOffscreenIndex_);
        nextOffscreenIndex_++;

        // クリアカラーを設定
        offscreenTarget->SetClearColor(desc.clearColor);

        // ターゲットをマップに登録
        RenderTarget* targetPtr = offscreenTarget.get();
        targets_[desc.name] = std::move(offscreenTarget);

        // 記述子を保存（リサイズ時に使用）
        descriptors_[desc.name] = desc;

#ifdef _DEBUG
        std::string msg = "[RenderTargetManager] Created RenderTarget: " + desc.name + 
                         " (Index: " + std::to_string(nextOffscreenIndex_ - 1) + ")\n";
        OutputDebugStringA(msg.c_str());
#endif

        return targetPtr;
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
        // 自動リサイズ対象のターゲットをリサイズ
        // 注意: 現在のOffscreenRenderTargetの実装では動的リサイズは未対応のため、
        // 将来の拡張のためのAPIとして定義のみ
        
        (void)newWidth;
        (void)newHeight;

#ifdef _DEBUG
        std::string msg = "[RenderTargetManager] ResizeAutoTargets called (width: " + 
                         std::to_string(newWidth) + ", height: " + std::to_string(newHeight) + ")\n";
        OutputDebugStringA(msg.c_str());
        OutputDebugStringA("[RenderTargetManager] NOTE: Auto-resize is not yet implemented for OffscreenRenderTarget.\n");
#endif

        // TODO: 将来の実装
        // - 各ターゲットの記述子を確認
        // - autoResize == true のものを再作成
    }
}
