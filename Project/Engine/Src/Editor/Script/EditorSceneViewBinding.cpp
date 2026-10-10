#include "pch.h"
#include "Editor/Script/EditorScriptBinding.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Editor/ImGui/Gizmo.h"
#include "Editor/Scene/ScenePicking.h"
#include "Editor/Scene/SceneViewTools.h"
#include "Editor/Script/EditorGUIState.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"

#include <angelscript.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <scriptarray/scriptarray.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    using Script::ScriptGameObject;

    namespace
    {
        bool g_inSceneGUI = false;

        /// スクリプトのハンドルにカーソルが乗っていた最後のフレーム
        int g_handleOverFrame = -2;

        /// @brief 同じフレームの中で、マウスの下を調べ直さない
        struct PickCache
        {
            int frame = -1;
            std::optional<ScenePicking::Hit> hit;
        };
        PickCache g_meshPick;
        PickCache g_objectPick;

        /// @brief OnSceneGUI の中でなければスクリプトの例外にする
        const SceneViewContext* RequireScene(const char* scope, const char* function)
        {
            const SceneViewContext* const context = SceneViewTools::Get().Current();
            if (g_inSceneGUI && context && context->camera) {
                return context;
            }
            ThrowScriptException(std::string(scope) + "::" + function + " は EditorWindow の OnSceneGUI の中でだけ使えます");
            return nullptr;
        }

        const SceneViewContext* RequireSceneView(const char* function)
        {
            return RequireScene("SceneView", function);
        }

        const SceneViewContext* RequireHandles(const char* function)
        {
            return RequireScene("Handles", function);
        }

        ImU32 ToU32(const Vector4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(SrgbToLinear(color.x, color.y, color.z, color.w));
        }

        bool IsValidMouseButton(int button)
        {
            return button >= 0 && button < ImGuiMouseButton_COUNT;
        }

        /// @brief カーソルがギズモかハンドルの上にあるか（そのクリックは道具へ渡さない）
        bool IsOverHandle()
        {
            return Gizmo::IsOver() || ImGuizmo::IsUsingAny() || g_handleOverFrame >= ImGui::GetFrameCount() - 1;
        }

        Vector2 MouseInView(const SceneViewContext& context)
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            return Vector2{ mouse.x - context.position.x, mouse.y - context.position.y };
        }

        ScenePicking::Ray MouseRay(const SceneViewContext& context)
        {
            const Vector2 mouse = MouseInView(context);
            return ScenePicking::ScreenToRay(Vector2{ mouse.x / context.size.x, mouse.y / context.size.y }, *context.camera);
        }

        const std::optional<ScenePicking::Hit>& Pick(const SceneViewContext& context, PickCache& cache, ScenePicking::Fallback fallback)
        {
            const int frame = ImGui::GetFrameCount();
            if (cache.frame != frame) {
                cache.frame = frame;
                cache.hit = (context.hovered && context.objects)
                    ? ScenePicking::Raycast(*context.objects, MouseRay(context), fallback)
                    : std::nullopt;
            }
            return cache.hit;
        }

        // ---------------------------------------------------------------- SceneView

        bool IsHovered()
        {
            const SceneViewContext* const context = RequireSceneView("isHovered");
            return context && context->hovered;
        }

        Vector2 GetMousePosition()
        {
            const SceneViewContext* const context = RequireSceneView("mousePosition");
            return context ? MouseInView(*context) : Vector2{};
        }

        Vector2 GetSize()
        {
            const SceneViewContext* const context = RequireSceneView("size");
            return context ? Vector2{ context->size.x, context->size.y } : Vector2{};
        }

        Vector3 GetCameraPosition()
        {
            const SceneViewContext* const context = RequireSceneView("cameraPosition");
            return context ? context->camera->GetPosition() : Vector3{};
        }

        Vector3 GetCameraForward()
        {
            const SceneViewContext* const context = RequireSceneView("cameraForward");
            return context ? context->camera->GetForward() : Vector3{ 0.0f, 0.0f, 1.0f };
        }

        bool GetMouseRay(Vector3& origin, Vector3& direction)
        {
            const SceneViewContext* const context = RequireSceneView("GetMouseRay");
            if (!context) {
                return false;
            }
            const ScenePicking::Ray ray = MouseRay(*context);
            origin = ray.origin;
            direction = ray.direction;
            return context->hovered;
        }

        bool RaycastMouse(Vector3& point, Vector3& normal)
        {
            const SceneViewContext* const context = RequireSceneView("RaycastMouse");
            if (!context) {
                return false;
            }
            const std::optional<ScenePicking::Hit>& hit = Pick(*context, g_meshPick, ScenePicking::Fallback::None);
            if (!hit) {
                return false;
            }
            point = hit->point;
            normal = hit->normal;
            return true;
        }

        ScriptGameObject* GetObjectUnderMouse()
        {
            const SceneViewContext* const context = RequireSceneView("GetObjectUnderMouse");
            if (!context) {
                return nullptr;
            }
            const std::optional<ScenePicking::Hit>& hit = Pick(*context, g_objectPick, ScenePicking::Fallback::Sphere);
            return hit ? ScriptGameObject::CreateForObject(hit->object) : nullptr;
        }

        bool MouseToPlane(float height, Vector3& point)
        {
            const SceneViewContext* const context = RequireSceneView("MouseToPlane");
            if (!context || !context->hovered) {
                return false;
            }
            const ScenePicking::Ray ray = MouseRay(*context);
            if (std::abs(ray.direction.y) < 1.0e-6f) {
                return false;
            }
            const float t = (height - ray.origin.y) / ray.direction.y;
            if (t <= 0.0f) {
                return false;
            }
            point = Vector3(ray.origin.x + ray.direction.x * t, height, ray.origin.z + ray.direction.z * t);
            return true;
        }

        bool WorldToScreen(const Vector3& world, Vector2& screen)
        {
            const SceneViewContext* const context = RequireSceneView("WorldToScreen");
            ImVec2 position;
            if (!context || !SceneOverlay::WorldToScreen(world, position)) {
                return false;
            }
            screen = Vector2{ position.x - context->position.x, position.y - context->position.y };
            return true;
        }

        bool IsMouseClicked(int button)
        {
            const SceneViewContext* const context = RequireSceneView("IsMouseClicked");
            return context && context->hovered && IsValidMouseButton(button) && !IsOverHandle()
                && ImGui::IsMouseClicked(button);
        }

        bool IsMouseDown(int button)
        {
            const SceneViewContext* const context = RequireSceneView("IsMouseDown");
            return context && context->hovered && IsValidMouseButton(button) && !IsOverHandle()
                && ImGui::IsMouseDown(button);
        }

        bool IsMouseReleased(int button)
        {
            return RequireSceneView("IsMouseReleased") && IsValidMouseButton(button) && ImGui::IsMouseReleased(button);
        }

        bool IsMouseDragging(int button)
        {
            const SceneViewContext* const context = RequireSceneView("IsMouseDragging");
            return context && context->hovered && IsValidMouseButton(button) && !IsOverHandle()
                && ImGui::IsMouseDragging(button);
        }

        bool IsKeyPressed(int key, bool repeat)
        {
            if (!RequireSceneView("IsKeyPressed")) {
                return false;
            }
            // Game のウィンドウが選ばれていて、文字を打っていないときだけ
            const ImGuiKey imguiKey = ToImGuiKey(key);
            return imguiKey != ImGuiKey_None && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(imguiKey, repeat);
        }

        bool GetCtrl()
        {
            return RequireSceneView("ctrl") && ImGui::GetIO().KeyCtrl;
        }

        bool GetShift()
        {
            return RequireSceneView("shift") && ImGui::GetIO().KeyShift;
        }

        bool GetAlt()
        {
            return RequireSceneView("alt") && ImGui::GetIO().KeyAlt;
        }

        void UseMouse()
        {
            if (RequireSceneView("UseMouse")) {
                SceneViewTools::Get().RequestMouse();
            }
        }

        // ---------------------------------------------------------------- Handles

        void DrawLine(const Vector3& from, const Vector3& to, const Vector4& color, float thickness)
        {
            if (RequireHandles("DrawLine")) {
                SceneOverlay::Line(from, to, ToU32(color), thickness);
            }
        }

        void DrawPolyline(const CScriptArray& points, const Vector4& color, float thickness, bool closed)
        {
            if (!RequireHandles("DrawPolyline")) {
                return;
            }
            std::vector<Vector3> positions;
            positions.reserve(points.GetSize());
            for (asUINT i = 0; i < points.GetSize(); ++i) {
                positions.push_back(*static_cast<const Vector3*>(points.At(i)));
            }
            SceneOverlay::Polyline(positions.data(), positions.size(), closed, ToU32(color), thickness);
        }

        void DrawWireCube(const Vector3& center, const Vector3& size, const Vector4& color, float thickness)
        {
            if (RequireHandles("DrawWireCube")) {
                SceneOverlay::WireBox(center, size, ToU32(color), thickness);
            }
        }

        void DrawWireDisc(const Vector3& center, const Vector3& normal, float radius, const Vector4& color, float thickness)
        {
            if (RequireHandles("DrawWireDisc")) {
                SceneOverlay::WireDisc(center, normal, radius, ToU32(color), thickness);
            }
        }

        void DrawWireSphere(const Vector3& center, float radius, const Vector4& color, float thickness)
        {
            if (RequireHandles("DrawWireSphere")) {
                SceneOverlay::WireSphere(center, radius, ToU32(color), thickness);
            }
        }

        void DrawSolidDisc(const Vector3& center, const Vector3& normal, float radius, const Vector4& color)
        {
            if (RequireHandles("DrawSolidDisc")) {
                SceneOverlay::SolidDisc(center, normal, radius, ToU32(color));
            }
        }

        void DrawSolidQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector4& color)
        {
            if (RequireHandles("DrawSolidQuad")) {
                SceneOverlay::SolidQuad(a, b, c, d, ToU32(color));
            }
        }

        void DrawDot(const Vector3& position, float size, const Vector4& color)
        {
            if (RequireHandles("DrawDot")) {
                SceneOverlay::Dot(position, size, ToU32(color));
            }
        }

        void Label(const Vector3& position, const std::string& text, const Vector4& color)
        {
            if (RequireHandles("Label")) {
                SceneOverlay::Label(position, text, ToU32(color));
            }
        }

        Vector3 PositionHandle(int id, const Vector3& position)
        {
            const SceneViewContext* const context = RequireHandles("PositionHandle");
            if (!context) {
                return position;
            }
            Matrix4x4 view = context->camera->GetViewMatrix();
            Matrix4x4 projection = context->camera->GetProjectionMatrix();

            // 回転はオイラー角の版を明示する（クォータニオン版と曖昧になるため）
            const Vector3 noRotation{ 0.0f, 0.0f, 0.0f };
            Matrix4x4 world = MathCore::Matrix::MakeAffine(Vector3{ 1.0f, 1.0f, 1.0f }, noRotation, position);

            // 選んだ物のギズモと別の ID にして、同じフレームに何個でも出せるようにする
            ImGuizmo::PushID(id);
            ImGuizmo::SetOrthographic(false);
            const bool changed = ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0], ImGuizmo::TRANSLATE,
                ImGuizmo::WORLD, &world.m[0][0]);
            const bool over = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            ImGuizmo::PopID();

            if (over) {
                g_handleOverFrame = ImGui::GetFrameCount();
                SceneViewTools::Get().RequestMouse();
            }
            if (!changed) {
                return position;
            }
            Vector3 translation;
            Vector3 rotation;
            Vector3 scale;
            ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &translation.x, &rotation.x, &scale.x);
            return translation;
        }
    }

    SceneGUIScope::SceneGUIScope()
    {
        g_inSceneGUI = true;
    }

    SceneGUIScope::~SceneGUIScope()
    {
        g_inSceneGUI = false;
    }

    bool RegisterEditorSceneView(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        Script::BindingRegistrar r(engine);

        r.Namespace("SceneView");
        r.Function("bool get_isHovered() property", asFUNCTION(IsHovered));
        r.Function("Vector2 get_mousePosition() property", asFUNCTION(GetMousePosition));
        r.Function("Vector2 get_size() property", asFUNCTION(GetSize));
        r.Function("Vector3 get_cameraPosition() property", asFUNCTION(GetCameraPosition));
        r.Function("Vector3 get_cameraForward() property", asFUNCTION(GetCameraForward));
        r.Function("bool GetMouseRay(Vector3 &out origin, Vector3 &out direction)", asFUNCTION(GetMouseRay));
        r.Function("bool RaycastMouse(Vector3 &out point, Vector3 &out normal)", asFUNCTION(RaycastMouse));
        r.Function("GameObject@ GetObjectUnderMouse()", asFUNCTION(GetObjectUnderMouse));
        r.Function("bool MouseToPlane(float height, Vector3 &out point)", asFUNCTION(MouseToPlane));
        r.Function("bool WorldToScreen(const Vector3 &in world, Vector2 &out screen)", asFUNCTION(WorldToScreen));
        r.Function("bool IsMouseClicked(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseClicked));
        r.Function("bool IsMouseDown(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseDown));
        r.Function("bool IsMouseReleased(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseReleased));
        r.Function("bool IsMouseDragging(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseDragging));
        r.Function("bool IsKeyPressed(Key key, bool repeat = true)", asFUNCTION(IsKeyPressed));
        r.Function("bool get_ctrl() property", asFUNCTION(GetCtrl));
        r.Function("bool get_shift() property", asFUNCTION(GetShift));
        r.Function("bool get_alt() property", asFUNCTION(GetAlt));
        r.Function("void UseMouse()", asFUNCTION(UseMouse));

        r.Namespace("Handles");
        r.Function("void DrawLine(const Vector3 &in from, const Vector3 &in to, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f), float thickness = 2)",
            asFUNCTION(DrawLine));
        r.Function("void DrawPolyline(const array<Vector3> &in points, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f), float thickness = 2, bool closed = false)",
            asFUNCTION(DrawPolyline));
        r.Function("void DrawWireCube(const Vector3 &in center, const Vector3 &in size, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f), float thickness = 2)",
            asFUNCTION(DrawWireCube));
        r.Function("void DrawWireDisc(const Vector3 &in center, const Vector3 &in normal, float radius, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f), float thickness = 2)",
            asFUNCTION(DrawWireDisc));
        r.Function("void DrawWireSphere(const Vector3 &in center, float radius, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f), float thickness = 2)",
            asFUNCTION(DrawWireSphere));
        r.Function("void DrawSolidDisc(const Vector3 &in center, const Vector3 &in normal, float radius, const Vector4 &in color)",
            asFUNCTION(DrawSolidDisc));
        r.Function("void DrawSolidQuad(const Vector3 &in a, const Vector3 &in b, const Vector3 &in c, const Vector3 &in d, const Vector4 &in color)",
            asFUNCTION(DrawSolidQuad));
        r.Function("void DrawDot(const Vector3 &in position, float size, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f))",
            asFUNCTION(DrawDot));
        r.Function("void Label(const Vector3 &in position, const string &in text, const Vector4 &in color = Vector4(1.0f, 1.0f, 1.0f, 1.0f))",
            asFUNCTION(Label));
        r.Function("Vector3 PositionHandle(int id, const Vector3 &in position)", asFUNCTION(PositionHandle));

        r.Namespace("");
        return r.Succeeded();
    }
}

#endif // CORE_EDITOR
