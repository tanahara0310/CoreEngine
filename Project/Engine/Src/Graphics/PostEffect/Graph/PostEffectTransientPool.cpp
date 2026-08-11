#include "pch.h"
#include "PostEffectTransientPool.h"

#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"

namespace CoreEngine
{
    namespace {
        /// 一時ターゲットの登録名の接頭辞。番号はプール内のスロット番号
        constexpr const char* kTransientNamePrefix = "PostEffectTransient";
    }

    std::string PostEffectTransientPool::Acquire(
        RenderTargetManager* manager, uint32_t width, uint32_t height, DXGI_FORMAT format)
    {
        if (!manager || width == 0 || height == 0) {
            return {};
        }

        if (cursor_ >= slots_.size()) {
            Slot slot;
            slot.name = kTransientNamePrefix + std::to_string(slots_.size());
            slots_.push_back(std::move(slot));
        }

        Slot& slot = slots_[cursor_];

        // 寸法・フォーマットが変わったとき、または実体が失われているときだけ作り直す
        const bool needsCreate = (slot.width != width) || (slot.height != height)
            || (slot.format != format) || !manager->HasRenderTarget(slot.name);
        if (needsCreate) {
            RenderTargetDescriptor desc(slot.name);
            desc.width  = width;
            desc.height = height;
            desc.format = format;
            // ポストエフェクトの中間に深度は要らない。既定のままだと 1 枚ごとに
            // 使われない D32 が付いてくる（1080p で 8.3MB/枚）
            desc.needsDepthStencil = false;
            // 寸法はここで明示するのでウィンドウ追従はしない
            desc.autoResize = false;

            manager->RemoveRenderTarget(slot.name);
            if (!manager->CreateRenderTarget(desc)) {
                return {};
            }
            slot.width  = width;
            slot.height = height;
            slot.format = format;
        }

        ++cursor_;
        acquiredThisFrame_.push_back(slot.name);
        return slot.name;
    }
}
