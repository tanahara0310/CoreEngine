#include "pch.h"
#include "CanvasViewport.h"

#ifdef USE_IMGUI

#include "EngineSystem/EngineSystem.h"
#include "Scene/SceneManager.h"
#include "ObjectCommon/GameObjectManager.h"
#include "ObjectCommon/GameObject.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/ImGui/ImGuiManager.h"
#include "Utility/Debug/ImGui/SceneViewport.h"
#include "Utility/Debug/ImGui/ObjectSelector.h"
#include "WinApp/WinApp.h"
#include "UI/UIImage.h"
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

        /// @brief EngineSystem 経由で ObjectSelector を取得する（取れなければ nullptr）
        ObjectSelector* AcquireObjectSelector(EngineSystem* engine)
        {
            if (!engine) { return nullptr; }
            auto* debug = engine->GetDebugSubsystem();
            if (!debug) { return nullptr; }
            auto* imgui = debug->GetImGuiManager();
            if (!imgui) { return nullptr; }
            auto* sceneViewport = imgui->GetSceneViewport();
            if (!sceneViewport) { return nullptr; }
            return sceneViewport->GetObjectSelector();
        }

        /// @brief 基準解像度上の UI 矩形（回転なし AABB）を計算
        UIRect ComputeImageRect(const UILayout& layout,
                                const ImVec2& canvasMin,
                                const Vector2& referenceSize,
                                float scale)
        {
            Vector2 anchorPoint = GetAnchorPoint(layout.anchor, referenceSize);
            float cx = anchorPoint.x + layout.anchoredPos.x;
            float cy = anchorPoint.y + layout.anchoredPos.y;

            float left   = cx - layout.pivot.x         * layout.size.x;
            float top    = cy - layout.pivot.y         * layout.size.y;
            float right  = cx + (1.0f - layout.pivot.x) * layout.size.x;
            float bottom = cy + (1.0f - layout.pivot.y) * layout.size.y;

            UIRect r;
            r.min = ImVec2(canvasMin.x + left  * scale, canvasMin.y + top    * scale);
            r.max = ImVec2(canvasMin.x + right * scale, canvasMin.y + bottom * scale);
            return r;
        }

        /// @brief 回転を考慮した4頂点クワッドを計算（スクリーン座標）
        UIQuad ComputeImageQuad(const UILayout& layout,
                                const ImVec2& canvasMin,
                                const Vector2& referenceSize,
                                float scale)
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
                return ImVec2(
                    canvasMin.x + (cx + rx2) * scale,
                    canvasMin.y + (cy + ry2) * scale);
            };

            UIQuad q;
            q.tl     = rotPt(lx, ty);
            q.tr     = rotPt(rx, ty);
            q.bl     = rotPt(lx, by);
            q.br     = rotPt(rx, by);
            q.center = ImVec2(canvasMin.x + cx * scale, canvasMin.y + cy * scale);
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

    void CanvasViewport::DrawCanvasViewport()
    {
        if (!engine_) { return; }

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
        if (auto* renderManager = engine_->GetComponent<RenderManager>()) {
            if (auto* uiRenderer = dynamic_cast<UIRenderer*>(
                    renderManager->GetRenderer(RenderPassType::UI))) {
                referenceSize = uiRenderer->GetScreenSize();
            }
        }

        // 基準解像度のアスペクト比を保持したまま、コンテンツ領域に内接させる
        const float aspect = referenceSize.x / referenceSize.y;
        float drawW = contentRegionSize.x;
        float drawH = drawW / aspect;
        if (drawH > contentRegionSize.y) {
            drawH = contentRegionSize.y;
            drawW = drawH * aspect;
        }

        // 基準解像度 → 描画領域のスケール係数
        // （Game ビューでは ImGui::Image が同じスケールで縮小表示している）
        const float scale = drawW / referenceSize.x;

        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        float offsetX = (contentRegionSize.x - drawW) * 0.5f;
        float offsetY = (contentRegionSize.y - drawH) * 0.5f;

        ImVec2 canvasMin = ImVec2(contentPos.x + offsetX, contentPos.y + offsetY);
        ImVec2 canvasMax = ImVec2(canvasMin.x + drawW,    canvasMin.y + drawH);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 背景：tile_black.png を全面に表示
        if (backgroundTextureHandlePtr_ != 0) {
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

        std::vector<UIImage*> uiImages;
        uiImages.reserve(64);
        for (const auto& obj : gom->GetAllObjects()) {
            if (!obj || !obj->IsActive() || obj->IsMarkedForDestroy()) { continue; }
            if (obj->GetRenderPassType() != RenderPassType::UI) { continue; }
            if (auto* img = dynamic_cast<UIImage*>(obj.get())) {
                uiImages.push_back(img);
            }
        }
        std::stable_sort(uiImages.begin(), uiImages.end(),
            [](const UIImage* a, const UIImage* b) {
                return a->GetLayout().sortOrder < b->GetLayout().sortOrder;
            });

        drawList->PushClipRect(canvasMin, canvasMax, true);

        for (auto* img : uiImages) {
            const UILayout& layout = img->GetLayout();
            UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = img->GetTextureGpuHandle();
            if (gpuHandle.ptr == 0) { continue; }

            Vector4 color = img->GetColor();
            ImU32 imColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(color.x, color.y, color.z, color.w));

            // 回転を反映するため AddImageQuad を使用
            drawList->AddImageQuad(
                static_cast<ImTextureID>(gpuHandle.ptr),
                quad.tl, quad.tr, quad.br, quad.bl,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f),
                imColor);
        }

        drawList->PopClipRect();

        // ─── Edit モード切替トグル（ウィンドウ右上） ──────────────────
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 8.0f, canvasMin.y + 8.0f));
        ImGui::Checkbox("Edit Mode", &editMode_);

        // 選択中要素が有効か検証（破棄/Active off 時は選択解除）
        if (selectedImage_) {
            bool stillValid = false;
            for (auto* img : uiImages) {
                if (img == selectedImage_) { stillValid = true; break; }
            }
            if (!stillValid) {
                selectedImage_ = nullptr;
                dragMode_ = DragMode::None;
            }
        }

        // 編集モード OFF 時は選択解除＆ドラッグ中断
        if (!editMode_) {
            selectedImage_ = nullptr;
            dragMode_ = DragMode::None;
        }

        // ─── マウス座標を基準解像度系へ変換 ─────────────────────────
        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseInCanvas = (mousePos.x >= canvasMin.x && mousePos.x <= canvasMax.x &&
                              mousePos.y >= canvasMin.y && mousePos.y <= canvasMax.y);
        Vector2 mouseRef = {
            (mousePos.x - canvasMin.x) / scale,
            (mousePos.y - canvasMin.y) / scale,
        };

        if (editMode_) {
            // ─── Edit モード：ギズモ操作 ───────────────────────────
            HandleGizmoInteraction(uiImages, canvasMin, canvasMax,
                                   referenceSize, scale,
                                   mousePos, mouseInCanvas, mouseRef,
                                   drawList);
        } else {
            // ─── ランタイム挙動：クリック / ホバー判定 ────────────
            if (mouseInCanvas) {
                for (int i = static_cast<int>(uiImages.size()) - 1; i >= 0; --i) {
                    UIImage* img = uiImages[i];
                    if (!img->IsInteractable()) { continue; }

                    const UILayout& layout = img->GetLayout();
                    UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

                    if (!PointInQuad(mousePos, quad)) { continue; }

                    UIRect rect = ComputeImageRect(layout, canvasMin, referenceSize, scale);
                    drawList->AddRectFilled(rect.min, rect.max, IM_COL32(255, 255, 255, 30));
                    img->InvokeOnHover();

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        drawList->AddRect(rect.min, rect.max, IM_COL32(255, 220, 0, 200), 0.0f, 0, 2.0f);
                        img->InvokeOnClick();

                        if (auto* selector = AcquireObjectSelector(engine_)) {
                            selector->SelectObject(img);
                        }
                    }
                    break;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    bool clickedOnAny = false;
                    for (auto* img : uiImages) {
                        if (!img->IsInteractable()) { continue; }
                        UIQuad quad = ComputeImageQuad(img->GetLayout(), canvasMin, referenceSize, scale);
                        if (PointInQuad(mousePos, quad)) { clickedOnAny = true; break; }
                    }
                    if (!clickedOnAny) {
                        if (auto* selector = AcquireObjectSelector(engine_)) {
                            if (auto* cur = selector->GetSelectedObject()) {
                                if (dynamic_cast<UIImage*>(cur)) { selector->ClearSelection(); }
                            }
                        }
                    }
                }
            }
        }

        ImGui::Dummy(contentRegionSize);

        ImGui::End();
    }

    // ════════════════════════════════════════════════════════════════
    //  Edit モード：選択 / 移動 / リサイズ ギズモ処理
    // ════════════════════════════════════════════════════════════════
    void CanvasViewport::HandleGizmoInteraction(
        const std::vector<UIImage*>& uiImages,
        const ImVec2& canvasMin,
        [[maybe_unused]] const ImVec2& canvasMax,
        const Vector2& referenceSize,
        float scale,
        const ImVec2& mousePos,
        bool mouseInCanvas,
        const Vector2& mouseRef,
        ImDrawList* drawList)
    {
        constexpr float kHandleSize    = 8.0f;
        constexpr float kRotHandleOffset = 24.0f; // 上辺中央からの距離（スクリーンpx）
        constexpr float kRotHandleRadius = 7.0f;

        auto makeHandleRect = [&](const UIQuad& quad, DragMode corner) -> UIRect {
            ImVec2 c;
            switch (corner) {
            case DragMode::ResizeTL: c = quad.tl; break;
            case DragMode::ResizeTR: c = quad.tr; break;
            case DragMode::ResizeBL: c = quad.bl; break;
            case DragMode::ResizeBR: c = quad.br; break;
            default:                 c = quad.tl; break;
            }
            return UIRect{
                ImVec2(c.x - kHandleSize, c.y - kHandleSize),
                ImVec2(c.x + kHandleSize, c.y + kHandleSize)
            };
        };
        auto pointInRect = [](const ImVec2& p, const UIRect& r) {
            return p.x >= r.min.x && p.x <= r.max.x &&
                   p.y >= r.min.y && p.y <= r.max.y;
        };
        auto pointInCircle = [](const ImVec2& p, const ImVec2& c, float r) {
            float dx = p.x - c.x, dy = p.y - c.y;
            return dx * dx + dy * dy <= r * r;
        };

        // 回転ハンドル位置：上辺中央から法線方向に kRotHandleOffset だけ離した位置
        auto getRotHandlePos = [&](const UIQuad& q) -> ImVec2 {
            // 上辺の中点
            ImVec2 topMid = { (q.tl.x + q.tr.x) * 0.5f, (q.tl.y + q.tr.y) * 0.5f };
            // 上辺の法線（上方向）
            ImVec2 edge   = { q.tr.x - q.tl.x, q.tr.y - q.tl.y };
            float  len    = std::sqrt(edge.x * edge.x + edge.y * edge.y);
            if (len < 0.001f) { return topMid; }
            ImVec2 normal = { -edge.y / len, edge.x / len };
            return ImVec2(topMid.x + normal.x * kRotHandleOffset,
                          topMid.y + normal.y * kRotHandleOffset);
        };

        // ─── ドラッグ中処理 ─────────────────────────────────────────
        if (dragMode_ != DragMode::None && selectedImage_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                Vector2 deltaRef = {
                    mouseRef.x - dragStartMouseRef_.x,
                    mouseRef.y - dragStartMouseRef_.y,
                };

                if (dragMode_ == DragMode::Move) {
                    selectedImage_->SetAnchoredPosition({
                        dragStartAnchoredPos_.x + deltaRef.x,
                        dragStartAnchoredPos_.y + deltaRef.y,
                    });
                } else if (dragMode_ == DragMode::Rotate) {
                    float angle = std::atan2(
                        mousePos.y - dragRotatePivotScreen_.y,
                        mousePos.x - dragRotatePivotScreen_.x);
                    float startAngle = std::atan2(
                        dragStartMouseRef_.y - dragRotatePivotScreen_.y,
                        dragStartMouseRef_.x - dragRotatePivotScreen_.x);
                    selectedImage_->SetUIRotation(
                        dragStartRotation_ + (angle - startAngle));
                } else {
                    // リサイズ
                    Vector2 pivot   = selectedImage_->GetPivot();
                    Vector2 anchorPt = GetAnchorPoint(selectedImage_->GetAnchor(), referenceSize);
                    Vector2 startCenter = {
                        anchorPt.x + dragStartAnchoredPos_.x,
                        anchorPt.y + dragStartAnchoredPos_.y,
                    };
                    Vector2 startTL = {
                        startCenter.x - pivot.x * dragStartSize_.x,
                        startCenter.y - pivot.y * dragStartSize_.y,
                    };
                    Vector2 startBR = {
                        startTL.x + dragStartSize_.x,
                        startTL.y + dragStartSize_.y,
                    };

                    // リサイズは回転ローカル系で行う
                    float cosR =  std::cos(-dragStartRotation_);
                    float sinR =  std::sin(-dragStartRotation_);
                    float localDx = deltaRef.x * cosR - deltaRef.y * sinR;
                    float localDy = deltaRef.x * sinR + deltaRef.y * cosR;

                    Vector2 newTL = startTL;
                    Vector2 newBR = startBR;
                    switch (dragMode_) {
                    case DragMode::ResizeTL: newTL.x += localDx; newTL.y += localDy; break;
                    case DragMode::ResizeTR: newBR.x += localDx; newTL.y += localDy; break;
                    case DragMode::ResizeBL: newTL.x += localDx; newBR.y += localDy; break;
                    case DragMode::ResizeBR: newBR.x += localDx; newBR.y += localDy; break;
                    default: break;
                    }

                    constexpr float kMinSize = 4.0f;
                    if (newBR.x - newTL.x < kMinSize) {
                        if (dragMode_ == DragMode::ResizeTL || dragMode_ == DragMode::ResizeBL) { newTL.x = newBR.x - kMinSize; }
                        else { newBR.x = newTL.x + kMinSize; }
                    }
                    if (newBR.y - newTL.y < kMinSize) {
                        if (dragMode_ == DragMode::ResizeTL || dragMode_ == DragMode::ResizeTR) { newTL.y = newBR.y - kMinSize; }
                        else { newBR.y = newTL.y + kMinSize; }
                    }

                    Vector2 newSize   = { newBR.x - newTL.x, newBR.y - newTL.y };
                    Vector2 newCenter = { newTL.x + pivot.x * newSize.x, newTL.y + pivot.y * newSize.y };
                    selectedImage_->SetSize(newSize);
                    selectedImage_->SetAnchoredPosition({ newCenter.x - anchorPt.x, newCenter.y - anchorPt.y });
                }
            } else {
                dragMode_ = DragMode::None;
            }
        }

        // ─── 新規クリック：選択 / ドラッグ開始 ──────────────────────
        if (dragMode_ == DragMode::None && mouseInCanvas &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (selectedImage_) {
                const UILayout& layout = selectedImage_->GetLayout();
                UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

                // 回転ハンドル優先判定
                ImVec2 rotHandle = getRotHandlePos(quad);
                if (pointInCircle(mousePos, rotHandle, kRotHandleRadius + 4.0f)) {
                    dragMode_              = DragMode::Rotate;
                    dragStartMouseRef_     = { mousePos.x, mousePos.y };
                    dragStartRotation_     = layout.rotation;
                    dragRotatePivotScreen_ = { quad.center.x, quad.center.y };
                    goto done_click;
                }

                // 4 隅ハンドル判定
                const DragMode corners[4] = {
                    DragMode::ResizeTL, DragMode::ResizeTR,
                    DragMode::ResizeBL, DragMode::ResizeBR,
                };
                for (DragMode c : corners) {
                    if (pointInRect(mousePos, makeHandleRect(quad, c))) {
                        dragMode_             = c;
                        dragStartMouseRef_    = mouseRef;
                        dragStartAnchoredPos_ = layout.anchoredPos;
                        dragStartSize_        = layout.size;
                        dragStartRotation_    = layout.rotation;
                        goto done_click;
                    }
                }
            }

            {
                // 本体クリック → 選択 + 移動開始
                UIImage* picked = nullptr;
                for (int i = static_cast<int>(uiImages.size()) - 1; i >= 0; --i) {
                    UIQuad q = ComputeImageQuad(uiImages[i]->GetLayout(), canvasMin, referenceSize, scale);
                    if (PointInQuad(mousePos, q)) { picked = uiImages[i]; break; }
                }
                if (picked) {
                    selectedImage_        = picked;
                    const UILayout& layout = picked->GetLayout();
                    dragMode_             = DragMode::Move;
                    dragStartMouseRef_    = mouseRef;
                    dragStartAnchoredPos_ = layout.anchoredPos;
                    dragStartSize_        = layout.size;
                    dragStartRotation_    = layout.rotation;
                    if (auto* selector = AcquireObjectSelector(engine_)) {
                        selector->SelectObject(picked);
                    }
                } else {
                    selectedImage_ = nullptr;
                    if (auto* selector = AcquireObjectSelector(engine_)) {
                        if (auto* cur = selector->GetSelectedObject()) {
                            if (dynamic_cast<UIImage*>(cur)) { selector->ClearSelection(); }
                        }
                    }
                }
            }
            done_click:;
        }

        // ─── 選択枠・ハンドルの描画 ──────────────────────────────────
        if (selectedImage_) {
            const UILayout& layout = selectedImage_->GetLayout();
            UIQuad quad = ComputeImageQuad(layout, canvasMin, referenceSize, scale);

            // 選択枠（回転クワッドの4辺）
            drawList->AddQuad(quad.tl, quad.tr, quad.br, quad.bl, IM_COL32(0, 200, 255, 220), 2.0f);

            // 4 隅リサイズハンドル
            const DragMode corners[4] = {
                DragMode::ResizeTL, DragMode::ResizeTR,
                DragMode::ResizeBL, DragMode::ResizeBR,
            };
            for (DragMode c : corners) {
                UIRect h = makeHandleRect(quad, c);
                drawList->AddRectFilled(h.min, h.max, IM_COL32(0, 200, 255, 255));
                drawList->AddRect(h.min, h.max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
            }

            // 回転ハンドル（上辺中央から伸びる線 + 円）
            ImVec2 topMid    = { (quad.tl.x + quad.tr.x) * 0.5f, (quad.tl.y + quad.tr.y) * 0.5f };
            ImVec2 rotHandle = getRotHandlePos(quad);
            drawList->AddLine(topMid, rotHandle, IM_COL32(0, 200, 255, 200), 1.5f);
            drawList->AddCircleFilled(rotHandle, kRotHandleRadius, IM_COL32(0, 200, 255, 255));
            drawList->AddCircle(rotHandle, kRotHandleRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);
        }
    }

} // namespace CoreEngine

#endif // USE_IMGUI
