#include "pch.h"
#include "GameObject/Component/Core/MissingComponent.h"

#include <utility>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    MissingComponent::MissingComponent(std::string typeName)
        : typeName_(std::move(typeName))
#ifdef USE_IMGUI
        , inspectorName_("読み込めない型: " + typeName_)
#endif
    {
    }

#ifdef USE_IMGUI
    bool MissingComponent::DrawInspector()
    {
        ImGui::TextWrapped("型「%s」が見つからないので、保存データをそのまま持っています。", typeName_.c_str());
        UI::Hint("スクリプトのコンパイルに失敗しているか、型の名前が変わったときに出ます。保存しても中身は消えません。");
        if (!parameters_.empty()) {
            UI::Separator();
            ImGui::TextUnformatted(parameters_.dump(2).c_str());
        }
        return false;
    }
#endif
}
