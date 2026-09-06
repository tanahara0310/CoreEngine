#include "pch.h"
#include "CanvasViewport.h"

#ifdef USE_IMGUI

#include "EngineSystem/EngineSystem.h"
#include "Scene/SceneManager.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/GameObject.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Editor/ImGui/Gizmo.h"
#include "WinApp/WinApp.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "GameObject/GameObjectManager.h"
#include "Text/FontManager.h"
#include "UI/UIAnchor.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace CoreEngine
{
    namespace
    {
        // tile_black.png のパス（Engine 側固定）
        constexpr const char* kBackgroundTexturePath = "tile_black.png";

        struct UIRect
        {
            ImVec2 min;
            ImVec2 max;
        };

        /// @brief 回転を考慮した4頂点クワッド（スクリーン座標）
        struct UIQuad
        {
            ImVec2 tl, tr, bl, br;
            ImVec2 center;
        };

        /// @brief 基準解像度上の UI 矩形（回転なし AABB）を計算
        UIRect ComputeImageRect(const UILayout& layout,
                                const ImVec2& canvasMin,
                                const Vector2& referenceSize,
                                const Vector2& scale)
        {
            Vector2 anchorPoint = GetAnchorPoint(layout.anchor, referenceSize);
            float cx = anchorPoint.x + layout.anchoredPos.x;
            float cy = anchorPoint.y + layout.anchoredPos.y;

            float left   = cx - layout.pivot.x         * layout.size.x;
            float top    = cy - layout.pivot.y         * layout.size.y;
            float right  = cx + (1.0f - layout.pivot.x) * layout.size.x;
            float bottom = cy + (1.0f - layout.pivot.y) * layout.size.y;

            UIRect r;
            r.min = ImVec2(canvasMin.x + left  * scale.x, canvasMin.y + top    * scale.y);
            r.max = ImVec2(canvasMin.x + right * scale.x, canvasMin.y + bottom * scale.y);
            return r;
        }

        /// @brief 回転を考慮した4頂点クワッドを計算（スクリーン座標）
        UIQuad ComputeImageQuad(const UILayout& layout,
                                const ImVec2& canvasMin,
                                const Vector2& referenceSize,
                                const Vector2& scale)
        {
            Vector2 anchorPoint = GetAnchorPoint(layout.anchor, referenceSize);
            float cx = anchorPoint.x + layout.anchoredPos.x;
            float cy = anchorPoint.y + layout.anchoredPos.y;

            float lx = -layout.pivot.x         * layout.size.x;
            float rx = (1.0f - layout.pivot.x)  * layout.size.x;
            float ty = -layout.pivot.y         * layout.size.y;
            float by = (1.0f - layout.pivot.y)  * layout.size.y;

            float cosR = std::cos(layout.rotation);
            float sinR = std::sin(layout.rotation);

            auto rotPt = [&](float ox, float oy) -> ImVec2 {
                float rx2 = ox * cosR - oy * sinR;
                float ry2 = ox * sinR + oy * cosR;
                // 「基準解像度で回してから画面へ引き伸ばす」順序は実際の描画と同じ。
                // 逆にすると縦横比の違う画面で回転がずれる
                return ImVec2(
                    canvasMin.x + (cx + rx2) * scale.x,
                    canvasMin.y + (cy + ry2) * scale.y);
            };

            UIQuad q;
            q.tl     = rotPt(lx, ty);
            q.tr     = rotPt(rx, ty);
            q.bl     = rotPt(lx, by);
            q.br     = rotPt(rx, by);
            q.center = ImVec2(canvasMin.x + cx * scale.x, canvasMin.y + cy * scale.y);
            return q;
        }

        /// @brief 点が回転矩形（クワッド）内にあるか（各辺の法線 cross 判定）
        bool PointInQuad(const ImVec2& p, const UIQuad& q)
        {
            auto cross2D = [](ImVec2 e, ImVec2 v) {
                return e.x * v.y - e.y * v.x;
            };
            ImVec2 verts[4] = { q.tl, q.tr, q.br, q.bl };
            for (int i = 0; i < 4; ++i) {
                ImVec2 a = verts[i];
                ImVec2 b = verts[(i + 1) % 4];
                ImVec2 edge = { b.x - a.x, b.y - a.y };
                ImVec2 toP  = { p.x  - a.x, p.y  - a.y };
                if (cross2D(edge, toP) < 0.0f) { return false; }
            }
            return true;
        }

    } // anonymous namespace

    // ════════════════════════════════════════════════════════════════
    //  CanvasElement — UIImage / UIText を同じ操作で扱うための薄い受け皿
    // ════════════════════════════════════════════════════════════════
    const UILayout& CanvasViewport::CanvasElement::Layout() const
    {
        static const UILayout kEmpty{};
        if (image) { return image->GetLayout(); }
        if (text) { return text->GetLayout(); }
        return kEmpty;
    }

    Vector2 CanvasViewport::CanvasElement::Pivot() const
    {
        if (image) { return image->GetPivot(); }
        if (text) { return text->GetPivot(); }
        return { 0.5f, 0.5f };
    }

    UIAnchor CanvasViewport::CanvasElement::Anchor() const
    {
        if (image) { return image->GetAnchor(); }
        if (text) { return text->GetAnchor(); }
        return UIAnchor::Center;
    }

    void CanvasViewport::CanvasElement::SetAnchoredPosition(const Vector2& position) const
    {
        if (image) { image->SetAnchoredPosition(position); }
        if (text) { text->SetAnchoredPosition(position); }
    }

    void CanvasViewport::CanvasElement::SetRotation(float radians) const
    {
        if (image) { image->SetUIRotation(radians); }
        if (text) { text->SetUIRotation(radians); }
    }

    void CanvasViewport::CanvasElement::SetSize(const Vector2& size) const
    {
        if (image) { image->SetSize(size); }
        // テキストはテキストフィールド（文字を流し込む枠）の大きさになる。
        // 幅は折り返し幅、高さは縦揃えの基準として効く
        if (text) { text->SetFieldSize(size); }
    }

    GameObject* CanvasViewport::GetSelection() const
    {
        return sceneDebugEditor_ ? sceneDebugEditor_->GetSelectedObject() : selectedObject_;
    }

    void CanvasViewport::SetSelection(GameObject* object)
    {
        selectedObject_ = object;
        if (sceneDebugEditor_) { sceneDebugEditor_->SelectObject(object); }
    }

    const CanvasViewport::CanvasElement* CanvasViewport::FindSelected(
        const std::vector<CanvasElement>& elements) const
    {
        GameObject* selection = GetSelection();
        if (!selection) { return nullptr; }
        for (const CanvasElement& element : elements) {
            if (element.object == selection) { return &element; }
        }
        return nullptr;
    }

    void CanvasViewport::Initialize(EngineSystem* engine)
    {
        engine_ = engine;

        auto& tm = TextureManager::GetInstance();
        auto handle = tm.Load(kBackgroundTexturePath);
        backgroundTextureHandlePtr_ = static_cast<unsigned long long>(handle.gpuHandle.ptr);

        DirectX::TexMetadata meta = tm.GetMetadata(kBackgroundTexturePath);
        backgroundTextureSize_.x = static_cast<float>(meta.width);
        backgroundTextureSize_.y = static_cast<float>(meta.height);
    }

    void CanvasViewport::DrawCanvasViewport(unsigned long long gameTextureHandlePtr,
        SceneDebugEditor* sceneDebugEditor)
    {
        if (!engine_) { return; }

        sceneDebugEditor_ = sceneDebugEditor;

        // 背景にゲームの描画結果そのものを敷けるか。
        // 敷けるなら UI の模写は一切せず、ギズモだけを重ねる（＝見えているものが結果）
        const bool useLivePreview = (gameTextureHandlePtr != 0);

        if (!ImGui::Begin("Canvas", nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground)) {
            ImGui::End();
            return;
        }

        ImVec2 contentRegionSize = ImGui::GetContentRegionAvail();
        if (contentRegionSize.x <= 1.0f || contentRegionSize.y <= 1.0f) {
            ImGui::End();
            return;
        }

        // UIRenderer から「基準解像度」を取得（Game ビューが UI 配置に使っているのと同じサイズ）
        Vector2 referenceSize = { 1280.0f, 720.0f };
        if (auto* renderManager = engine_->GetService<RenderManager>()) {
            if (auto* uiRenderer = dynamic_cast<UIRenderer*>(
                    renderManager->GetRenderer(RenderPassType::UI))) {
                referenceSize = uiRenderer->GetScreenSize();
            }
        }

        // ライブプレビュー時はゲーム描画先の縦横比に合わせる。
        // UI は基準解像度を画面全体へ写す投影なので、
        // 「描画結果の矩形全体 = 基準解像度の矩形全体」で対応が取れる
        float aspect = referenceSize.x / referenceSize.y;
        if (useLivePreview) {
            const float clientW = static_cast<float>(WinApp::GetCurrentClientWidthStatic());
            const float clientH = static_cast<float>(WinApp::GetCurrentClientHeightStatic());
            if (clientW > 0.0f && clientH > 0.0f) { aspect = clientW / clientH; }
        }

        float drawW = contentRegionSize.x;
        float drawH = drawW / aspect;
        if (drawH > contentRegionSize.y) {
            drawH = contentRegionSize.y;
            drawW = drawH * aspect;
        }

        // 基準解像度 → 描画領域のスケール係数。
        // 縦横比が基準解像度と違う画面では x と y で値が変わるので Vector2 で持つ
        const Vector2 scale = { drawW / referenceSize.x, drawH / referenceSize.y };

        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        float offsetX = (contentRegionSize.x - drawW) * 0.5f;
        float offsetY = (contentRegionSize.y - drawH) * 0.5f;

        ImVec2 canvasMin = ImVec2(contentPos.x + offsetX, contentPos.y + offsetY);
        ImVec2 canvasMax = ImVec2(canvasMin.x + drawW,    canvasMin.y + drawH);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 背景：ゲームの描画結果（無ければ tile_black.png）
        if (useLivePreview) {
            drawList->AddImage(
                static_cast<ImTextureID>(gameTextureHandlePtr),
                canvasMin, canvasMax);
        } else if (backgroundTextureHandlePtr_ != 0) {
            drawList->AddImage(
                static_cast<ImTextureID>(backgroundTextureHandlePtr_),
                canvasMin, canvasMax);
        } else {
            drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(20, 20, 20, 255));
        }

        auto* sceneManager = engine_->GetSceneManager();
        auto* gom = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
        if (!gom) {
            ImGui::Dummy(contentRegionSize);
            ImGui::End();
            return;
        }

        std::vector<CanvasElement> elements;
        elements.reserve(64);
        for (const auto& obj : gom->GetAllObjects()) {
            if (!obj || !obj->IsActive() || obj->IsMarkedForDestroy()) { continue; }

            const RenderPassType pass = obj->GetRenderPassType();
            if (pass == RenderPassType::UI) {
                if (auto* image = dynamic_cast<UIImage*>(obj.get())) {
                    elements.push_back(CanvasElement{ obj.get(), image, nullptr });
                }
            } else if (pass == RenderPassType::UIText) {
                if (auto* text = dynamic_cast<UIText*>(obj.get())) {
                    elements.push_back(CanvasElement{ obj.get(), nullptr, text });
                }
            }
        }
        // テキストは UI の上に描かれるので、Canvas 上でも後ろに置いて重ね順を合わせる
        std::stable_sort(elements.begin(), elements.end(),
            [](const CanvasElement& a, const CanvasElement& b) {
                const int passA = (a.text != nullptr) ? 1 : 0;
                const int passB = (b.text != nullptr) ? 1 : 0;
                if (passA != passB) { return passA < passB; }
                return a.Layout().sortOrder < b.Layout().sortOrder;
            });

        drawList->PushClipRect(canvasMin, canvasMax, true);

        // ライブプレビューでは背景がそのまま結果なので、ここで描き足すと二重になる
        for (const CanvasElement& element : elements) {
            if (useLivePreview) { break; }

            const UILayout& layout = element.Layout();
            UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

            if (element.image) {
                D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = element.image->GetTextureGpuHandle();
                if (gpuHandle.ptr == 0) { continue; }

                Vector4 color = element.image->GetColor();
                ImU32 imColor = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(color.x, color.y, color.z, color.w));

                // 回転を反映するため AddImageQuad を使用
                drawList->AddImageQuad(
                    static_cast<ImTextureID>(gpuHandle.ptr),
                    quad.tl, quad.tr, quad.br, quad.bl,
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f),
                    ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f),
                    imColor);
            } else if (element.text) {
                // 背景が渡らなかったときの退避表示。
                // MSDF アトラスは ImGui の DrawList では描けない（中央値の再構成と
                // 距離場のアンチエイリアスが要る）ので、枠と ImGui フォントで
                // 「どこに何があるか」だけ分かるようにする
                const Vector4 color = element.text->GetColor();
                const ImU32 imColor = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(color.x, color.y, color.z, color.w));

                drawList->AddQuad(quad.tl, quad.tr, quad.br, quad.bl,
                    IM_COL32(120, 190, 230, 90), 1.0f);
                drawList->AddText(quad.tl, imColor, element.text->GetText().c_str());
            }
        }

        drawList->PopClipRect();

        // ─── ツールバー（左上） ────────────────────────────────────
        // 背景はゲームの絵なので、下敷きを敷かないと文字が読めなくなる
        constexpr float kToolbarPad = 6.0f;
        const float toolbarHeight = ImGui::GetFrameHeight() + kToolbarPad * 2.0f;
        drawList->AddRectFilled(
            ImVec2(canvasMin.x, canvasMin.y),
            ImVec2(canvasMax.x, canvasMin.y + toolbarHeight),
            IM_COL32(18, 18, 22, 190));

        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + kToolbarPad, canvasMin.y + kToolbarPad));
        ImGui::Checkbox("Edit Mode", &editMode_);

        // Edit モードのときだけ、UI を追加できるようにする
        if (editMode_) {
            DrawCreateUI(gom);

            ImGui::SameLine();
            ImGui::TextDisabled("クリックで選択 / ギズモで移動・回転・拡縮（W E R）/ 矢印キーで微調整");
        } else if (!useLivePreview) {
            ImGui::SameLine();
            ImGui::TextDisabled("（簡易表示：実際の字形は Game ビューを参照）");
        }

        // ─── マウス座標を基準解像度系へ変換 ─────────────────────────
        ImVec2 mousePos = ImGui::GetMousePos();
        // ツールバーのボタンや、手前に重なった別ウィンドウの上では要素を掴ませない。
        // 矩形の内外だけで判定すると「Edit Mode を押したら背後の文字も選ばれる」ことになる
        bool mouseInCanvas = (mousePos.x >= canvasMin.x && mousePos.x <= canvasMax.x &&
                              mousePos.y >= canvasMin.y && mousePos.y <= canvasMax.y) &&
                             ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
        Vector2 mouseRef = {
            (mousePos.x - canvasMin.x) / scale.x,
            (mousePos.y - canvasMin.y) / scale.y,
        };

        if (editMode_) {
            // ─── Edit モード：ギズモ操作 ───────────────────────────
            HandleGizmoInteraction(elements, canvasMin, ImVec2(drawW, drawH),
                                   referenceSize, scale,
                                   mousePos, mouseInCanvas,
                                   drawList);
            HandleKeyboardNudge(elements);
        } else {
            // ─── ランタイム挙動：クリック / ホバー判定 ────────────
            if (mouseInCanvas) {
                for (int i = static_cast<int>(elements.size()) - 1; i >= 0; --i) {
                    UIImage* img = elements[i].image;
                    if (!img || !img->IsInteractable()) { continue; }

                    const UILayout& layout = img->GetLayout();
                    UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

                    if (!PointInQuad(mousePos, quad)) { continue; }

                    UIRect rect = ComputeImageRect(layout, canvasMin, referenceSize, scale);
                    drawList->AddRectFilled(rect.min, rect.max, IM_COL32(255, 255, 255, 30));
                    img->InvokeOnHover();

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        drawList->AddRect(rect.min, rect.max, IM_COL32(255, 220, 0, 200), 0.0f, 0, 2.0f);
                        img->InvokeOnClick();
                    }
                    break;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    bool clickedOnAny = false;
                    for (const CanvasElement& element : elements) {
                        UIImage* img = element.image;
                        if (!img || !img->IsInteractable()) { continue; }
                        UIQuad quad = ComputeImageQuad(img->GetLayout(), canvasMin, referenceSize, scale);
                        if (PointInQuad(mousePos, quad)) { clickedOnAny = true; break; }
                    }
                    (void)clickedOnAny;
                }
            }
        }

        ImGui::Dummy(contentRegionSize);

        ImGui::End();
    }

    // ════════════════════════════════════════════════════════════════
    //  Edit モード：矢印キーでの微調整
    // ════════════════════════════════════════════════════════════════
    void CanvasViewport::HandleKeyboardNudge(const std::vector<CanvasElement>& elements)
    {
        // 文字入力中に矢印キーを奪うと、インスペクタの入力欄でカーソルが動かせなくなる
        if (ImGui::GetIO().WantTextInput) { return; }

        const CanvasElement* selected = FindSelected(elements);
        if (!selected) { return; }

        // 1px 単位で置きたい場面と、大きく寄せたい場面の両方があるので Shift で切り替える
        const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;

        Vector2 delta{ 0.0f, 0.0f };
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true)) { delta.x -= step; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { delta.x += step; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true)) { delta.y -= step; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true)) { delta.y += step; }
        if (delta.x == 0.0f && delta.y == 0.0f) { return; }

        const Vector2 pos = selected->Layout().anchoredPos;
        selected->SetAnchoredPosition({ pos.x + delta.x, pos.y + delta.y });
    }

    // ════════════════════════════════════════════════════════════════
    //  Edit モード：UI の追加
    // ════════════════════════════════════════════════════════════════
    void CanvasViewport::DrawCreateUI(GameObjectManager* gom)
    {
        if (!gom) { return; }

        ImGui::SameLine();
        if (!ImGui::Button("＋ テキストを追加")) { return; }

        auto text = std::make_unique<UIText>();
        UIText* created = static_cast<UIText*>(gom->AddObject(std::move(text)));
        if (!created) { return; }

        // 画面中央へ、すぐ見える大きさで置く。
        // フォントは AddObject → Initialize() の中で FontManager の既定が入る
        created->SetText("新しいテキスト");
        created->SetFontSize(32.0f);
        created->SetAnchor(UIAnchor::Center);
        created->SetPivot({ 0.5f, 0.5f });
        created->SetAnchoredPosition({ 0.0f, 0.0f });
        created->SetSortOrder(100);

        // 追加した直後に選択状態にして、そのまま動かせるようにする
        SetSelection(created);
    }

    // ════════════════════════════════════════════════════════════════
    //  Edit モード：選択とギズモ操作
    // ════════════════════════════════════════════════════════════════
    void CanvasViewport::HandleGizmoInteraction(
        const std::vector<CanvasElement>& elements,
        const ImVec2& canvasMin,
        const ImVec2& canvasSize,
        const Vector2& referenceSize,
        const Vector2& scale,
        const ImVec2& mousePos,
        bool mouseInCanvas,
        ImDrawList* drawList)
    {
        // ギズモは Canvas ウィンドウの描画リストへ出す。
        // 指定しないと最前面のウィンドウへ描かれて Canvas の上に乗らない
        Gizmo::Prepare(canvasMin, canvasSize);
        ImGuizmo::SetDrawlist(drawList);

        // W / E / R でモードを切り替える。Game ビューと同じ割り当てにしておかないと、
        // 「Canvas ではショートカットが効かない」という食い違いが残る。
        // 文字入力中はインスペクタの入力欄へ譲る
        if (sceneDebugEditor_ && mouseInCanvas && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                sceneDebugEditor_->SetGizmoMode(Gizmo::Mode::Translate);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                sceneDebugEditor_->SetGizmoMode(Gizmo::Mode::Rotate);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                sceneDebugEditor_->SetGizmoMode(Gizmo::Mode::Scale);
            }
        }

        // 操作モードはツールバーとも共有する。
        // UI だけ別の操作系になっていると、掴み方を覚え直すことになる
        const Gizmo::Mode mode = sceneDebugEditor_
            ? sceneDebugEditor_->GetGizmoMode()
            : Gizmo::Mode::Translate;

        // ─── ①ギズモ操作 ───────────────────────────────────────
        const CanvasElement* selected = FindSelected(elements);
        if (selected) {
            UILayout layout = selected->Layout();
            if (Gizmo::ManipulateUI(layout, referenceSize, mode)) {
                selected->SetAnchoredPosition(layout.anchoredPos);
                selected->SetRotation(layout.rotation);

                // 大きさは拡縮モードのときだけ渡す。
                // 行列の分解は移動でもスケールを誤差ぶん揺らすので、
                // 毎回渡すと「動かしただけでテキストフィールドの自動調整が切れる」
                if (mode == Gizmo::Mode::Scale) {
                    selected->SetSize(layout.size);
                }
            }
        }

        // ─── ②クリックで選択 ───────────────────────────────────
        // ギズモを掴んでいる / 触れている間は選択を変えない。
        // 変えてしまうと、ハンドルを掴んだ瞬間に別の要素へ飛ぶ
        if (mouseInCanvas && !Gizmo::IsUsing() && !Gizmo::IsOver() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const CanvasElement* picked = nullptr;
            for (int i = static_cast<int>(elements.size()) - 1; i >= 0; --i) {
                UIQuad q = ComputeImageQuad(elements[i].Layout(), canvasMin, referenceSize, scale);
                if (PointInQuad(mousePos, q)) { picked = &elements[i]; break; }
            }
            SetSelection(picked ? picked->object : nullptr);
        }

        // ─── ③選択枠 ──────────────────────────────────────────
        // 掴む場所はギズモなので、枠は「どこが選ばれているか」を示すだけ
        selected = FindSelected(elements);
        if (selected) {
            UIQuad quad = ComputeImageQuad(selected->Layout(), canvasMin, referenceSize, scale);
            drawList->AddQuad(quad.tl, quad.tr, quad.br, quad.bl,
                IM_COL32(0, 200, 255, 220), 2.0f);
        }
    }

} // namespace CoreEngine

#endif // USE_IMGUI
