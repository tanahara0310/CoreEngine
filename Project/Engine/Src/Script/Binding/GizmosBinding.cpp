#include "pch.h"
#include "Script/Binding/GizmosBinding.h"

#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"

#ifdef CORE_EDITOR
#include "Camera/Camera.h"
#include "Editor/Scene/SceneViewTools.h"
#include "Editor/Script/EditorGUIState.h"

#include <imgui.h>

#include <cmath>
#endif

#include <angelscript.h>

namespace CoreEngine::Script
{
    namespace
    {
        Vector4 g_color{ 1.0f, 1.0f, 1.0f, 1.0f };

#ifdef CORE_EDITOR
        /// 線の太さ（ピクセル）
        constexpr float kThickness = 1.5f;

        bool g_drawing = false;

        /// @brief 描いてよいか（ギズモの関数の中で、シーンビューを描いている間）
        bool CanDraw()
        {
            return g_drawing && Editor::SceneViewTools::Get().Current();
        }

        ImU32 CurrentColor()
        {
            return ImGui::ColorConvertFloat4ToU32(
                Editor::ScriptBinding::SrgbToLinear(g_color.x, g_color.y, g_color.z, g_color.w));
        }
#endif

        Vector4 GetColor()
        {
            return g_color;
        }

        void SetColor(const Vector4& color)
        {
            g_color = color;
        }

        void DrawLine(const Vector3& from, const Vector3& to)
        {
#ifdef CORE_EDITOR
            if (CanDraw()) {
                Editor::SceneOverlay::Line(from, to, CurrentColor(), kThickness);
            }
#else
            (void)from;
            (void)to;
#endif
        }

        void DrawRay(const Vector3& from, const Vector3& direction)
        {
            DrawLine(from, Vector3(from.x + direction.x, from.y + direction.y, from.z + direction.z));
        }

        void DrawWireCube(const Vector3& center, const Vector3& size)
        {
#ifdef CORE_EDITOR
            if (CanDraw()) {
                Editor::SceneOverlay::WireBox(center, size, CurrentColor(), kThickness);
            }
#else
            (void)center;
            (void)size;
#endif
        }

        void DrawWireSphere(const Vector3& center, float radius)
        {
#ifdef CORE_EDITOR
            if (CanDraw()) {
                Editor::SceneOverlay::WireSphere(center, radius, CurrentColor(), kThickness);
            }
#else
            (void)center;
            (void)radius;
#endif
        }

        void DrawSphere(const Vector3& center, float radius)
        {
#ifdef CORE_EDITOR
            if (!CanDraw()) {
                return;
            }
            // 画面の上で、中心から半径ぶん離れた点までの長さを半径にした円で塗る
            ImVec2 middle;
            ImVec2 edge;
            const Editor::SceneViewContext* const context = Editor::SceneViewTools::Get().Current();
            const Vector3 sideways = context && context->camera ? context->camera->GetRight() : Vector3(1.0f, 0.0f, 0.0f);
            if (Editor::SceneOverlay::WorldToScreen(center, middle)
                && Editor::SceneOverlay::WorldToScreen(
                    Vector3(center.x + sideways.x * radius, center.y + sideways.y * radius, center.z + sideways.z * radius), edge)) {
                const float dx = edge.x - middle.x;
                const float dy = edge.y - middle.y;
                Editor::SceneOverlay::Dot(center, 2.0f * std::sqrt(dx * dx + dy * dy), CurrentColor());
            }
#else
            (void)center;
            (void)radius;
#endif
        }
    }

#ifdef CORE_EDITOR
    GizmoDrawScope::GizmoDrawScope()
    {
        g_drawing = true;
        g_color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }

    GizmoDrawScope::~GizmoDrawScope()
    {
        g_drawing = false;
        g_color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }
#endif

    bool RegisterGizmosBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        r.Namespace("Gizmos");
        r.Function("Vector4 get_color() property", asFUNCTION(GetColor));
        r.Function("void set_color(const Vector4 &in) property", asFUNCTION(SetColor));
        r.Function("void DrawLine(const Vector3 &in from, const Vector3 &in to)", asFUNCTION(DrawLine));
        r.Function("void DrawRay(const Vector3 &in from, const Vector3 &in direction)", asFUNCTION(DrawRay));
        r.Function("void DrawWireCube(const Vector3 &in center, const Vector3 &in size)", asFUNCTION(DrawWireCube));
        r.Function("void DrawWireSphere(const Vector3 &in center, float radius)", asFUNCTION(DrawWireSphere));
        r.Function("void DrawSphere(const Vector3 &in center, float radius)", asFUNCTION(DrawSphere));
        r.Namespace("");
        return r.Succeeded();
    }
}
