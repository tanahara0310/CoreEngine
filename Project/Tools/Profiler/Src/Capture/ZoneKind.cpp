#include "Capture/ZoneKind.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Text/TextFormat.h"

#include <array>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief パスの中に、区切りで挟まれたフォルダ名 folder があるか（区切りは \ と / のどちらでもよい）
        bool HasFolder(std::string_view file, std::string_view folder)
        {
            for (size_t pos = file.find(folder); pos != std::string_view::npos; pos = file.find(folder, pos + 1)) {
                const bool startsAtSeparator = pos > 0 && (file[pos - 1] == '\\' || file[pos - 1] == '/');
                const size_t end = pos + folder.size();
                const bool endsAtSeparator = end < file.size() && (file[end] == '\\' || file[end] == '/');
                if (startsAtSeparator && endsAtSeparator) {
                    return true;
                }
            }
            return false;
        }
    }

    ZoneKind ClassifyZone(std::string_view name, std::string_view file)
    {
        if (name == "Wait For Frame Slot" || name == "Present") {
            return ZoneKind::Wait;
        }

        const std::string_view fileName = Text::FileName(file);
        if (fileName == "ScriptComponent.cpp") {
            return ZoneKind::Script;
        }
        if (fileName == "Framework.cpp") {
            if (name == "Update") {
                return ZoneKind::Update;
            }
            if (name == "Render" || name == "PrepareRender") {
                return ZoneKind::Render;
            }
            return ZoneKind::Frame;
        }
        if (fileName == "StartupSequence.cpp" || fileName == "ThreadPool.cpp"
            || name.starts_with("Shader:") || name.starts_with("AssetInfo:")) {
            return ZoneKind::Load;
        }
        if (name == "ImGui Draw" || fileName == "RenderGraph.cpp" || HasFolder(file, "Graphics")) {
            return ZoneKind::Render;
        }
        if (fileName == "Scene.cpp" || fileName == "GameObjectManager.cpp"
            || HasFolder(file, "GameObject") || HasFolder(file, "Physics") || HasFolder(file, "Animation")) {
            return ZoneKind::Update;
        }
        return ZoneKind::Other;
    }

    const char* ZoneKindLabel(ZoneKind kind)
    {
        static constexpr std::array<const char*, static_cast<size_t>(ZoneKind::Count)> kLabels = {
            "更新", "スクリプト", "描画", "待ち", "フレームの枠", "読み込み", "ほか",
        };
        const size_t index = static_cast<size_t>(kind);
        return index < kLabels.size() ? kLabels[index] : "";
    }

    ImVec4 ZoneKindColor(ZoneKind kind)
    {
        switch (kind) {
        case ZoneKind::Update: return Theme::kOk;
        case ZoneKind::Script: return Theme::kScript;
        case ZoneKind::Render: return Theme::kAccentHover;
        case ZoneKind::Wait: return Theme::FromSrgb(138, 138, 150);
        case ZoneKind::Frame: return Theme::kActive;
        case ZoneKind::Load: return Theme::kWarn;
        default: return Theme::kTextMute;
        }
    }
}
