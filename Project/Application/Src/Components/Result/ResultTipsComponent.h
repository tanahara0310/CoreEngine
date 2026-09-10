#pragma once

#include "GameObject/Component/Core/IComponent.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace GameComponents
{
    /// @brief ResultScene で順番に表示する Tips の設定を保持するコンポーネント。
    class ResultTipsComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "ResultTips"; }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

        const std::vector<std::string>& GetTips() const { return tips_; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "リザルトTips"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }
        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 1.0f;
            outRgba[1] = 0.78f;
            outRgba[2] = 0.28f;
            outRgba[3] = 1.0f;
        }
        bool DrawInspector() override;

    private:
        static constexpr std::size_t kEditBufferSize = 2048;
        void SyncEditBuffers();

        std::vector<std::array<char, kEditBufferSize>> editBuffers_;
#endif

        std::vector<std::string> tips_;
    };
}
