#include "pch.h"
#include "PostEffectSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectPresetManager.h"

namespace CoreEngine
{
    void PostEffectSettingsSection::Serialize(json& out) const
    {
        auto* manager = engine_ ? engine_->GetComponent<PostEffectManager>() : nullptr;
        if (!manager) {
            return;
        }

        out = PostEffectPresetManager::CaptureToJson(manager);

        // フェードの進行度は実行時のシーン遷移状態のため自動保存しない
        // （欠損キーは ApplyFromJson が 0（フェードなし）として扱う）
        if (out.contains("fadeEffect")) {
            out["fadeEffect"].erase("fadeAlpha");
        }
    }

    void PostEffectSettingsSection::Deserialize(const json& in)
    {
        auto* manager = engine_ ? engine_->GetComponent<PostEffectManager>() : nullptr;
        if (!manager) {
            return;
        }
        PostEffectPresetManager::ApplyFromJson(manager, in);
    }
}
