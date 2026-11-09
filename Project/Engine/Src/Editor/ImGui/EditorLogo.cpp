#include "pch.h"
#include "Editor/ImGui/EditorLogo.h"

#ifdef CORE_EDITOR

namespace CoreEngine::Editor
{
    namespace
    {
        /// Kind の並びと同じ順の画像
        constexpr const char* kFiles[] = {
            "Engine/Assets/Textures/Logo/CoreEngineMark.png",
            "Engine/Assets/Textures/Logo/CoreEngineLogo.png",
            "Engine/Assets/Textures/Logo/CoreEngineLogoStacked.png",
        };
    }

    EditorLogo::EditorLogo(GraphicsCore& graphics)
        : graphics_(graphics)
    {
    }

    ImTextureID EditorLogo::Texture(Kind kind)
    {
        return Get(kind).id;
    }

    float EditorLogo::Aspect(Kind kind)
    {
        const EditorImage& image = Get(kind);
        return image.height > 0.0f ? image.width / image.height : 1.0f;
    }

    const EditorImage& EditorLogo::Get(Kind kind)
    {
        Image& entry = images_[static_cast<size_t>(kind)];
        if (!entry.attempted) {
            // 読めなかったときも覚えて、次からは読み直さない
            entry.attempted = true;
            entry.image = LoadEditorImage(graphics_, kFiles[static_cast<size_t>(kind)]);
        }
        return entry.image;
    }
}

#endif // CORE_EDITOR
