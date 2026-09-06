#include "pch.h"
#include "Text3DObject.h"

#include "Camera/Camera.h"
#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Render/RenderManager.h"
#include "Scene/SceneSaveSystem.h"
#include "Text/FontManager.h"
#include "Text/MsdfFont.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef USE_IMGUI
#include "Editor/ImGui/Wrappers/ImGuiInput.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#include <imgui.h>
#endif

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        /// 距離場は輪郭の外側 pxRange/2 までしか情報を持たない。
        /// 端ぎりぎりは値が飽和しているので、少し内側を上限にする
        constexpr float kMaxOutlineSd = 0.45f;

        /// @brief ビルボードの回転行列を作る（平行移動なし）
        /// @param viewMatrix 描画に使うビューの行列
        /// @note 式はパーティクルの `ParticleRenderDataBuilder::CreateBillboardMatrix` と同じ。
        ///       あちらは private かつパーティクル固有のヘッダを引き連れているため、
        ///       テキストが要る 2 方式だけをここに置いている。
        Matrix4x4 MakeBillboardMatrix(const Matrix4x4& viewMatrix, Text3DBillboard mode)
        {
            // ビューの逆行列の 3x3 がそのままカメラの姿勢（right / up / forward）
            const Matrix4x4 invView = Matrix::Inverse(viewMatrix);

            if (mode == Text3DBillboard::ViewFacing) {
                Matrix4x4 billboard = invView;
                billboard.m[3][0] = 0.0f;
                billboard.m[3][1] = 0.0f;
                billboard.m[3][2] = 0.0f;
                return billboard;
            }

            // YAxisOnly: 上方向は world up に固定し、水平成分だけカメラへ向ける。
            // こうしないと見下ろし視点で文字が地面に寝てしまう
            const Vector3 cameraPosition = { invView.m[3][0], invView.m[3][1], invView.m[3][2] };
            const float horizontalLength =
                std::sqrt(cameraPosition.x * cameraPosition.x + cameraPosition.z * cameraPosition.z);

            Vector3 forward, right;
            if (horizontalLength < 0.0001f) {
                // 真上・真下から見ている。向きが決まらないので既定の姿勢にする
                forward = { 0.0f, 0.0f, 1.0f };
                right = { 1.0f, 0.0f, 0.0f };
            }
            else {
                forward = { cameraPosition.x / horizontalLength, 0.0f, cameraPosition.z / horizontalLength };
                right = { -forward.z, 0.0f, forward.x };
            }

            Matrix4x4 billboard = Matrix::Identity();
            billboard.m[0][0] = right.x;   billboard.m[0][1] = right.y;   billboard.m[0][2] = right.z;
            billboard.m[1][0] = 0.0f;      billboard.m[1][1] = 1.0f;      billboard.m[1][2] = 0.0f;
            billboard.m[2][0] = forward.x; billboard.m[2][1] = forward.y; billboard.m[2][2] = forward.z;
            return billboard;
        }
    }

#ifdef USE_IMGUI
    namespace
    {
        /// @brief 入力欄の作業バッファへ文字列を写す
        /// @details 入りきらない分は捨てる。ただし UTF-8 の途中で切ると
        ///          文字化けするので、多バイト文字の境界まで戻して切る
        template <size_t N>
        void CopyToEditBuffer(std::array<char, N>& buffer, const std::string& source)
        {
            size_t length = (std::min)(source.size(), N - 1);
            while (length > 0 &&
                (static_cast<unsigned char>(source[length]) & 0xC0) == 0x80) {
                --length; // 継続バイトの上にいる間は先頭バイトまで戻す
            }
            std::memcpy(buffer.data(), source.data(), length);
            buffer[length] = '\0';
        }
    }
#endif // USE_IMGUI

    namespace
    {
        /// @brief シーン JSON から Text3D を復元できるように型を登録する
        const bool kText3DTypeRegistered = [] {
            SceneSaveSystem::RegisterObjectType("Text3D",
                [] { return std::make_unique<Text3DObject>(); });
            return true;
            }();
    }

    void Text3DObject::Initialize()
    {
        auto* engine = GetEngineSystem();
        if (!engine) { return; }

        if (auto* renderManager = engine->GetService<RenderManager>()) {
            renderer_ = dynamic_cast<Text3DRenderer*>(
                renderManager->GetRenderer(RenderPassType::Text3D));
        }

        // 位置・回転・スケールはトランスフォームに持たせる。
        // エディタのギズモとインスペクタがそのまま使えるようになる
        transform_ = GetOrAddComponent<TransformComponent>();

        // フォントがまだ無いなら既定フォントを取る。
        // エディタ上で追加したテキストは指定が無い状態で生まれるため
        if (!font_) {
            if (auto* fontManager = engine->GetService<FontManager>()) {
                fontName_ = FontManager::kDefaultFontName;
                font_ = fontManager->AcquireNamed(fontName_);
            }
        }

        RebuildGeometry();
    }

    void Text3DObject::Initialize(MsdfFont* font, const std::string& textUtf8, const std::string& name)
    {
        if (!name.empty()) {
            SetName(name);
        }

        font_ = font;
        textUtf8_ = textUtf8;

        // 共通の初期化（レンダラー解決・トランスフォーム・頂点の構築）へ合流する。
        // font_ を先に入れてあるので既定フォントの取得はスキップされる
        Initialize();
    }

    void Text3DObject::SetFontByName(const std::string& fontName)
    {
        auto* engine = GetEngineSystem();
        if (!engine) { return; }

        auto* fontManager = engine->GetService<FontManager>();
        if (!fontManager) { return; }

        MsdfFont* resolved = fontManager->AcquireNamed(fontName);
        if (!resolved) { return; }

        fontName_ = fontName;
        if (font_ != resolved) {
            font_ = resolved;
            geometryDirty_ = true;
        }
    }

    void Text3DObject::SetText(const std::string& textUtf8)
    {
        if (textUtf8_ == textUtf8) { return; }
        textUtf8_ = textUtf8;
        geometryDirty_ = true;
    }

    void Text3DObject::SetFontSize(float worldUnitsPerEm)
    {
        if (fontSize_ == worldUnitsPerEm) { return; }
        fontSize_ = worldUnitsPerEm;

        // 頂点は em 単位なので通常は組み直さない。
        // 折り返し幅・フィールドはワールド単位で持っているので、
        // それらが有効なときだけ em 換算が変わって折り位置が動く
        if (wrapWidth_ > 0.0f || !fieldAutoFit_) { geometryDirty_ = true; }
    }

    void Text3DObject::SetWrapWidth(float worldUnits)
    {
        const float clamped = (std::max)(worldUnits, 0.0f);
        if (wrapWidth_ == clamped) { return; }
        wrapWidth_ = clamped;
        geometryDirty_ = true;
    }

    void Text3DObject::SetFieldSize(const Vector2& sizeWorld)
    {
        fieldAutoFit_ = false;
        fieldSize_ = sizeWorld;
        geometryDirty_ = true;
    }

    void Text3DObject::SetFieldAutoFit(bool enable)
    {
        if (fieldAutoFit_ == enable) { return; }
        fieldAutoFit_ = enable;
        geometryDirty_ = true;
    }

    void Text3DObject::SetAlign(TextAlignH horizontal, TextAlignV vertical)
    {
        if (alignH_ == horizontal && alignV_ == vertical) { return; }
        alignH_ = horizontal;
        alignV_ = vertical;
        geometryDirty_ = true;
    }

    void Text3DObject::SetLineSpacing(float scale)
    {
        if (lineSpacing_ == scale) { return; }
        lineSpacing_ = scale;
        geometryDirty_ = true;
    }

    void Text3DObject::SetPivot(const Vector2& pivot)
    {
        if (pivot_.x == pivot.x && pivot_.y == pivot.y) { return; }
        pivot_ = pivot;
        geometryDirty_ = true;
    }

    void Text3DObject::SetOutline(const Vector4& color, float widthEm)
    {
        style_.outlineColor = color;
        style_.outlineWidthEm = (std::max)(widthEm, 0.0f);
    }

    float Text3DObject::GetMaxOutlineWidth() const
    {
        if (!font_) { return 0.0f; }
        const float pxRange = font_->GetPxRange();
        if (pxRange <= 0.0f) { return 0.0f; }

        // em → 距離場の値。この逆数に上限値を掛けたものが表現できる最大幅
        const float sdUnitsPerEm = static_cast<float>(font_->GetGlyphPixelSize()) / pxRange;
        return (sdUnitsPerEm > 0.0f) ? (kMaxOutlineSd / sdUnitsPerEm) : 0.0f;
    }

    void Text3DObject::RebuildGeometry()
    {
        geometryDirty_ = false;
        glyphVertices_.clear();
        measuredSizeEm_ = { 0.0f, 0.0f };
        lineCount_ = 0;

        if (!font_ || !font_->IsValid()) { return; }

        // 折り返し幅とフィールドはワールド単位で持っているので em へ直す。
        // 組版は em で行うので、ここが唯一の単位変換になる
        // （UI 版が px を em へ直しているのと同じ位置づけ）
        const float wrapWidthWorld = fieldAutoFit_ ? wrapWidth_ : fieldSize_.x;

        TextGeometry::BuildParams params{};
        params.lineSpacing = lineSpacing_;
        params.wrapWidthEm = (wrapWidthWorld > 0.0f && fontSize_ > 0.0f)
            ? wrapWidthWorld / fontSize_
            : 0.0f;
        params.autoFitField = fieldAutoFit_;
        params.fieldEm = (fieldAutoFit_ || fontSize_ <= 0.0f)
            ? Vector2{ 0.0f, 0.0f }
            : Vector2{ fieldSize_.x / fontSize_, fieldSize_.y / fontSize_ };
        params.alignH = alignH_;
        params.alignV = alignV_;
        params.pivot = pivot_;
        // ワールド空間は Y 上正
        params.yAxisDown = false;
        params.maxGlyphs = Text3DRenderer::kMaxGlyphsPerText;

        const TextGeometry::BuildResult result =
            TextGeometry::Build(*font_, textUtf8_, params, glyphVertices_);

        measuredSizeEm_ = result.measuredSizeEm;
        lineCount_ = result.lineCount;
        lastGlyphGeneration_ = result.glyphGeneration;

        // 自動調整ならフィールドを文字列の大きさへ合わせる。
        // 空文字列（行が 1 つも組めなかった）のときは触らない
        if (fieldAutoFit_ && result.lineCount > 0) {
            fieldSize_ = GetMeasuredSize();
        }

        if (result.truncated && !glyphLimitWarned_) {
            glyphLimitWarned_ = true;
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
                "Text3D '{}': グリフ数が上限 {} を超えたため切り詰めました（要求 {}）",
                GetName(), Text3DRenderer::kMaxGlyphsPerText, result.requestedGlyphCount);
        }
    }

    Matrix4x4 Text3DObject::BuildWorldMatrix(const Matrix4x4& viewMatrix) const
    {
        // 頂点が em 単位なので、フォントサイズがそのままスケールになる
        const Vector3 fontScale = { fontSize_, fontSize_, 1.0f };
        constexpr Vector3 kNoRotation = { 0.0f, 0.0f, 0.0f };
        constexpr Vector3 kNoTranslation = { 0.0f, 0.0f, 0.0f };

        if (!transform_) {
            return Matrix::MakeAffine(fontScale, kNoRotation, kNoTranslation);
        }

        const WorldTransform& worldTransform = transform_->Get();

        if (billboard_ == Text3DBillboard::None) {
            // トランスフォームのワールド行列（親の階層込み）へそのまま乗せる
            return Matrix::MakeAffine(fontScale, kNoRotation, kNoTranslation)
                * worldTransform.GetWorldMatrix();
        }

        // ビルボードは姿勢をカメラから作るので、トランスフォームの回転は使わない。
        // 位置と拡縮だけを引き継ぐ
        const Vector3 objectScale = transform_->GetWorldScale();
        const Vector3 scale = {
            fontScale.x * objectScale.x,
            fontScale.y * objectScale.y,
            fontScale.z * objectScale.z,
        };

        Matrix4x4 world = Matrix::MakeAffine(scale, kNoRotation, kNoTranslation)
            * MakeBillboardMatrix(viewMatrix, billboard_);

        const Vector3 position = worldTransform.GetWorldPosition();
        world.m[3][0] = position.x;
        world.m[3][1] = position.y;
        world.m[3][2] = position.z;
        return world;
    }

    void Text3DObject::Draw(const Camera* camera)
    {
        // RenderGraph を経由しない直接呼び出し（レガシー経路）
        if (!camera) { return; }
        const Matrix4x4& viewMatrix = camera->GetViewMatrix();
        SubmitToRenderer(viewMatrix, viewMatrix * camera->GetProjectionMatrix());
    }

    void Text3DObject::Draw(const DrawViewInfo& view)
    {
        // ビュー行列はここから取る。レンダラーがカメラを読み直すと、
        // パスごとに違う行列を使う事故が起きる
        if (!view.view || !view.view->isValid) { return; }
        SubmitToRenderer(view.view->viewMatrix, view.view->viewProjection);
    }

    void Text3DObject::SubmitToRenderer(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjection)
    {
        if (!IsActive() || !renderer_ || !font_ || !font_->IsValid()) { return; }

        // 実行時ベイクで新しいグリフが増えていたら組み直す。
        // これが □ から本来の字へ差し替わる瞬間
        if (font_->GetGlyphGeneration() != lastGlyphGeneration_) {
            geometryDirty_ = true;
        }
        if (geometryDirty_) { RebuildGeometry(); }
        if (glyphVertices_.empty()) { return; }

        const Matrix4x4 world = BuildWorldMatrix(viewMatrix);

        // ここではドローコールを出さず、レンダラーのバッチへ積むだけ。
        // 実際の描画は EndPass（またはフォント・深度モードの切り替え）でまとめて出る
        renderer_->Submit(font_, glyphVertices_.data(), glyphVertices_.size(),
            world, viewProjection, style_, depthMode_);
    }

    json Text3DObject::OnSerialize() const
    {
        json j;
        j["active"] = IsActive();

        // フォントは名前だけ。実体の指定は FontManager 側に置く
        j["font"] = fontName_;
        j["text"] = textUtf8_;
        j["fontSize"] = fontSize_;
        j["lineSpacing"] = lineSpacing_;
        j["wrapWidth"] = wrapWidth_;

        j["fieldAutoFit"] = fieldAutoFit_;
        j["fieldSize"]["x"] = fieldSize_.x;
        j["fieldSize"]["y"] = fieldSize_.y;
        j["alignH"] = static_cast<int>(alignH_);
        j["alignV"] = static_cast<int>(alignV_);
        j["pivot"]["x"] = pivot_.x;
        j["pivot"]["y"] = pivot_.y;

        j["color"] = JsonManager::Vector4ToJson(style_.color);
        j["outlineColor"] = JsonManager::Vector4ToJson(style_.outlineColor);
        j["outlineWidth"] = style_.outlineWidthEm;
        j["weight"] = style_.weightEm;

        j["billboard"] = static_cast<int>(billboard_);
        j["depthMode"] = static_cast<int>(depthMode_);

        return j;
    }

    void Text3DObject::OnDeserialize(const json& j)
    {
        if (j.contains("active")) { SetActive(j["active"].get<bool>()); }

        // フォントを先に解決する（メトリクスが決まらないとレイアウトが組めない）
        if (j.contains("font") && j["font"].is_string()) {
            SetFontByName(j["font"].get<std::string>());
        }

        textUtf8_ = JsonManager::SafeGet<std::string>(j, "text", textUtf8_);
        fontSize_ = JsonManager::SafeGet<float>(j, "fontSize", fontSize_);
        lineSpacing_ = JsonManager::SafeGet<float>(j, "lineSpacing", lineSpacing_);
        wrapWidth_ = JsonManager::SafeGet<float>(j, "wrapWidth", wrapWidth_);

        fieldAutoFit_ = JsonManager::SafeGet<bool>(j, "fieldAutoFit", fieldAutoFit_);
        if (j.contains("fieldSize")) {
            fieldSize_.x = JsonManager::SafeGet<float>(j["fieldSize"], "x", fieldSize_.x);
            fieldSize_.y = JsonManager::SafeGet<float>(j["fieldSize"], "y", fieldSize_.y);
        }
        const int alignHIndex = JsonManager::SafeGet<int>(j, "alignH", static_cast<int>(alignH_));
        if (alignHIndex >= 0 && alignHIndex <= static_cast<int>(TextAlignH::Right)) {
            alignH_ = static_cast<TextAlignH>(alignHIndex);
        }
        const int alignVIndex = JsonManager::SafeGet<int>(j, "alignV", static_cast<int>(alignV_));
        if (alignVIndex >= 0 && alignVIndex <= static_cast<int>(TextAlignV::Bottom)) {
            alignV_ = static_cast<TextAlignV>(alignVIndex);
        }
        if (j.contains("pivot")) {
            pivot_.x = JsonManager::SafeGet<float>(j["pivot"], "x", pivot_.x);
            pivot_.y = JsonManager::SafeGet<float>(j["pivot"], "y", pivot_.y);
        }

        style_.color = JsonManager::SafeGetVector4(j, "color", style_.color);
        style_.outlineColor = JsonManager::SafeGetVector4(j, "outlineColor", style_.outlineColor);
        style_.outlineWidthEm = JsonManager::SafeGet<float>(j, "outlineWidth", style_.outlineWidthEm);
        style_.weightEm = JsonManager::SafeGet<float>(j, "weight", style_.weightEm);

        const int billboardIndex = JsonManager::SafeGet<int>(j, "billboard", static_cast<int>(billboard_));
        if (billboardIndex >= 0 && billboardIndex <= static_cast<int>(Text3DBillboard::YAxisOnly)) {
            billboard_ = static_cast<Text3DBillboard>(billboardIndex);
        }
        const int depthIndex = JsonManager::SafeGet<int>(j, "depthMode", static_cast<int>(depthMode_));
        if (depthIndex >= 0 && depthIndex <= static_cast<int>(Text3DDepthMode::Overlay)) {
            depthMode_ = static_cast<Text3DDepthMode>(depthIndex);
        }

        geometryDirty_ = true;
    }

#ifdef USE_IMGUI
    int Text3DObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const
    {
        if (maxTabs < 2) { return 0; }
        outTabs[0] = { "object_data.png", "配置",     {0.96f,0.65f,0.14f,1.0f}, {0.96f,0.65f,0.14f,0.25f} };
        outTabs[1] = { "material.png",    "テキスト", {0.30f,0.70f,0.90f,1.0f}, {0.30f,0.70f,0.90f,0.25f} };
        return 2;
    }

    bool Text3DObject::DrawInspectorTabContent(int tabIndex)
    {
        bool changed = false;

        switch (tabIndex)
        {
            // ── 0: 配置 ────────────────────────────────────────────
        case 0:
        {
            UI::SectionHeader("ビルボード");
            {
                static const char* kBillboardNames[] = { "なし", "カメラに向く", "Y軸のみ" };
                int billboardIndex = static_cast<int>(billboard_);
                if (ImGui::Combo("##billboard", &billboardIndex, kBillboardNames, 3)) {
                    billboard_ = static_cast<Text3DBillboard>(billboardIndex);
                    changed = true;
                }
                ImGui::TextDisabled("「なし」ならトランスフォームの回転がそのまま効きます");
            }

            UI::SectionHeader("深度");
            {
                static const char* kDepthNames[] = { "遮蔽される", "常に手前（オーバーレイ）" };
                int depthIndex = static_cast<int>(depthMode_);
                if (ImGui::Combo("##depthMode", &depthIndex, kDepthNames, 2)) {
                    depthMode_ = static_cast<Text3DDepthMode>(depthIndex);
                    changed = true;
                }
                ImGui::TextDisabled("ダメージ数値やネームプレートはオーバーレイ向き");
            }

            UI::SectionHeader("Pivot");
            {
                Vector2 pivotTmp = pivot_;
                if (UI::DragVec2("##pivot", pivotTmp, 0.01f, 0.0f, 1.0f)) {
                    SetPivot(pivotTmp);
                    changed = true;
                }
                ImGui::TextDisabled("0.5, 0.5 で置いた位置に文字の中心が来ます");
            }

            UI::SectionHeader("テキストフィールド");
            {
                bool autoFit = fieldAutoFit_;
                if (ImGui::Checkbox("文字に合わせる##fieldAutoFit", &autoFit)) {
                    SetFieldAutoFit(autoFit);
                    changed = true;
                }

                if (fieldAutoFit_) {
                    // 枠が文字に追従するので、折り返し幅は別に指定する
                    float wrapTmp = wrapWidth_;
                    if (UI::DragFloat("折り返し幅（0 で無効）##wrap",
                        wrapTmp, 0.05f, 0.0f, 1000.0f)) {
                        SetWrapWidth(wrapTmp);
                        changed = true;
                    }
                    ImGui::TextDisabled("枠は文字列を囲む大きさになります");
                }
                else {
                    Vector2 fieldTmp = fieldSize_;
                    if (UI::DragVec2("##fieldSize", fieldTmp, 0.05f, 0.01f, 1000.0f)) {
                        SetFieldSize(fieldTmp);
                        changed = true;
                    }
                    ImGui::TextDisabled("枠の幅で折り返します");
                }
                ImGui::TextDisabled("単位はワールド単位です");

                static const char* kAlignHNames[] = { "左", "中央", "右" };
                static const char* kAlignVNames[] = { "上", "中央", "下" };
                int alignHIndex = static_cast<int>(alignH_);
                int alignVIndex = static_cast<int>(alignV_);
                bool alignChanged = ImGui::Combo("横揃え##alignH", &alignHIndex, kAlignHNames, 3);
                alignChanged |= ImGui::Combo("縦揃え##alignV", &alignVIndex, kAlignVNames, 3);
                if (alignChanged) {
                    SetAlign(static_cast<TextAlignH>(alignHIndex),
                        static_cast<TextAlignV>(alignVIndex));
                    changed = true;
                }
            }
            break;
        }

        // ── 1: テキスト ────────────────────────────────────────
        case 1:
        {
            UI::SectionHeader("文字列");
            {
                // 入力中は ImGui がバッファを持つので、そうでない間だけ写し直す
                if (!editTextActive_) { CopyToEditBuffer(editTextBuffer_, textUtf8_); }

                const ImVec2 boxSize(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f);
                if (ImGui::InputTextMultiline("##text", editTextBuffer_.data(),
                    editTextBuffer_.size(), boxSize)) {
                    // 1 打鍵ごとに反映する。未収録の字はここで焼き足しの要求が出る
                    SetText(editTextBuffer_.data());
                    changed = true;
                }
                editTextActive_ = ImGui::IsItemActive();
                ImGui::TextDisabled("Enter で改行。日本語は IME でそのまま入力できます");
            }

            UI::SectionHeader("フォント");
            {
                auto* engine = GetEngineSystem();
                auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;

                // Engine/Assets/Font のファイルと、登録済みフォントから選ぶ
                if (fontManager) {
                    const std::vector<std::string> names = fontManager->GetSelectableFontNames();
                    if (!names.empty()) {
                        if (ImGui::BeginCombo("##fontPreset", fontName_.c_str())) {
                            for (const std::string& name : names) {
                                const bool selected = (name == fontName_);
                                if (ImGui::Selectable(name.c_str(), selected)) {
                                    SetFontByName(name);
                                    CopyToEditBuffer(editFontBuffer_, fontName_);
                                    changed = true;
                                }
                                if (selected) { ImGui::SetItemDefaultFocus(); }
                            }
                            ImGui::EndCombo();
                        }
                    }
                }

                if (!editFontActive_) { CopyToEditBuffer(editFontBuffer_, fontName_); }
                if (ImGui::InputText("##fontName", editFontBuffer_.data(),
                    editFontBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    SetFontByName(editFontBuffer_.data());
                    changed = true;
                }
                editFontActive_ = ImGui::IsItemActive();
                ImGui::TextDisabled("フォント名を入力して Enter");
            }

            UI::SectionHeader("文字の大きさ");
            {
                float sizeTmp = fontSize_;
                if (UI::DragFloat("ワールド単位/em##fontSize", sizeTmp, 0.01f, 0.001f, 100.0f)) {
                    // 頂点は em 単位なので、折り返しが無ければ再構築不要。
                    // ここが MSDF の効きどころで、近づいても輪郭は鋭いまま
                    SetFontSize(sizeTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("行間");
            {
                float spacingTmp = lineSpacing_;
                if (UI::DragFloat("倍##lineSpacing", spacingTmp, 0.01f, 0.1f, 4.0f)) {
                    SetLineSpacing(spacingTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("カラー");
            {
                Vector4 color = style_.color;
                if (UI::ColorEdit("##color", color)) {
                    style_.color = color;
                    changed = true;
                }
            }

            UI::SectionHeader("縁取り");
            {
                Vector4 outlineColor = style_.outlineColor;
                float outlineWidth = style_.outlineWidthEm;
                const float maxWidth = GetMaxOutlineWidth();

                bool outlineChanged = UI::ColorEdit("色##outline", outlineColor);
                outlineChanged |= UI::DragFloat("太さ(em)##outline", outlineWidth,
                    0.001f, 0.0f, maxWidth);
                if (outlineChanged) {
                    SetOutline(outlineColor, outlineWidth);
                    changed = true;
                }
                ImGui::TextDisabled("上限 %.3f em（pxRange を上げると広がる）", maxWidth);
            }

            UI::SectionHeader("太さ調整");
            {
                float weight = style_.weightEm;
                if (UI::DragFloat("em##weight", weight, 0.001f, -0.05f, 0.05f)) {
                    style_.weightEm = weight;
                    changed = true;
                }
            }

            UI::SectionHeader("情報");
            {
                const Vector2 measured = GetMeasuredSize();
                ImGui::Text("実寸: %.2f x %.2f / %u 行", measured.x, measured.y, lineCount_);
                ImGui::Text("グリフ数: %u / %u", GetGlyphCount(), Text3DRenderer::kMaxGlyphsPerText);
                if (renderer_) {
                    ImGui::Text("3Dテキスト全体: %u ドローコール / %u グリフ",
                        renderer_->GetLastFrameDrawCallCount(),
                        renderer_->GetLastFrameGlyphCount());
                }
                if (font_) {
                    const Vector2 atlas = font_->GetAtlasSize();
                    ImGui::Text("アトラス: %.0f x %.0f x%d枚 / pxRange %.1f",
                        atlas.x, atlas.y, font_->GetPageCount(), font_->GetPxRange());
                }
            }
            break;
        }

        default: break;
        }

        return changed;
    }
#endif // USE_IMGUI
}
