#include "pch.h"
#include "UI/UIImageComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "GameObject/Component/Core/ComponentService.h"
#include "GameObject/GameObject.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/UIBindings.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIMaskComponent.h"
#include "Utility/Algorithm/AssignIfChanged.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>
#include <cstring>

REFLECT_REGISTER(CoreEngine::UIImageComponent)
COMPONENT_REGISTER(CoreEngine::UIImageComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        /// @brief テクスチャを指していないときに貼る白
        constexpr const char* kWhiteTexture = "white1x1.png";
    }

    UIImageComponent::~UIImageComponent() = default;

    void UIImageComponent::Awake()
    {
        awoken_ = true;

        // 配置は兄弟の UI トランスフォームから取る。無ければ足す
        if (GameObject* const owner = GetOwner()) {
            rect_ = owner->GetOrAddComponent<RectTransformComponent>();
        }

        ResolveRenderer();
        CreateGpuResources();
        LoadTexture();
        materialSlot_.Resolve();
    }

    void UIImageComponent::ResolveRenderer()
    {
        auto* const renderManager = FindService<RenderManager>(*this);
        if (renderManager) {
            renderer_ = renderManager->GetRendererAs<UIRenderer>(RenderPassType::UI);
        }
    }

    void UIImageComponent::CreateGpuResources()
    {
        if (!renderer_) { return; }

        GraphicsCore* const graphics = renderer_->GetGraphicsCore();
        if (!graphics) { return; }

        // マテリアル（色の定数バッファ）
        material_ = std::make_unique<UIMaterialInstance>();
        material_->Initialize(*graphics);
        material_->SetColor(color_);
    }

    void UIImageComponent::LoadTexture()
    {
        auto& textureManager = TextureManager::GetInstance();
        if (!textureManager.IsInitialized()) { return; }

        const std::string path = textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string(kWhiteTexture);
        textureHandle_ = textureManager.Load(path);

        const DirectX::TexMetadata metadata = textureManager.GetMetadata(path);
        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);

        // 9 スライスの枠の幅は次に描くときに読み直す
        textureBorderRevision_ = UINT32_MAX;
        verticesDirty_ = true;
    }

    void UIImageComponent::SetTexture(const std::string& texturePath)
    {
        const Reflection::AssetRefValue previous = textureAsset_.GetValue();
        textureAsset_.SetPath(texturePath);

        // Awake より前は指す先だけを控え、Awake で読み込む。指す先が変わらなければ読み直さない
        if (awoken_ && !(textureAsset_.GetValue() == previous)) {
            LoadTexture();
        }
    }

    std::string UIImageComponent::GetTexturePath() const
    {
        return textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string();
    }

    void UIImageComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) {
            return;
        }
        textureAsset_.SetValue(value);
        if (awoken_) {
            LoadTexture();
        }
    }

    void UIImageComponent::SetNativeSize()
    {
        RectTransformComponent* const rect = GetRectTransform();
        if (rect && textureSize_.x > 0.0f && textureSize_.y > 0.0f) {
            rect->SetSize({ textureSize_.x / static_cast<float>(columns_), textureSize_.y / static_cast<float>(rows_) });
        }
    }

    void UIImageComponent::SetColumns(int columns)
    {
        columns_ = (std::max)(columns, 1);
        verticesDirty_ = true;
    }

    void UIImageComponent::SetRows(int rows)
    {
        rows_ = (std::max)(rows, 1);
        verticesDirty_ = true;
    }

    void UIImageComponent::SetFrame(int frame)
    {
        if (AssignIfChanged(frame_, frame)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetMaterial(const std::string& materialPath)
    {
        Reflection::AssetRefValue value;
        if (!materialPath.empty()) {
            AssetRef<ShaderMaterialAsset> reference(materialPath);
            value = reference.GetValue();
        }
        SetMaterialAsset(value);
    }

    void UIImageComponent::SetMaterialAsset(const Reflection::AssetRefValue& value)
    {
        if (materialSlot_.SetAsset(value) && awoken_) {
            materialSlot_.Resolve();
        }
    }

    void UIImageComponent::SetColor(const Vector4& color)
    {
        color_ = color;
        if (material_) {
            material_->SetColor(color_);
        }
    }

    void UIImageComponent::SetImageType(UIImageType type)
    {
        if (AssignIfChanged(imageType_, type)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetFillCenter(bool fill)
    {
        if (AssignIfChanged(fillCenter_, fill)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetBorderOverridden(bool overridden)
    {
        if (AssignIfChanged(overrideBorder_, overridden)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetBorderOverride(const Vector4& border)
    {
        const Vector4 clamped = {
            (std::max)(border.x, 0.0f), (std::max)(border.y, 0.0f),
            (std::max)(border.z, 0.0f), (std::max)(border.w, 0.0f),
        };
        if (clamped.x != borderOverride_.x || clamped.y != borderOverride_.y
            || clamped.z != borderOverride_.z || clamped.w != borderOverride_.w) {
            borderOverride_ = clamped;
            verticesDirty_ = true;
        }
    }

    Vector4 UIImageComponent::GetBorder() const
    {
        return overrideBorder_ ? borderOverride_ : textureBorder_;
    }

    void UIImageComponent::SetFillMethod(UIFillMethod method)
    {
        if (AssignIfChanged(fillMethod_, method)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetFillAmount(float amount)
    {
        if (AssignIfChanged(fillAmount_, std::clamp(amount, 0.0f, 1.0f))) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetFillOrigin(UIFillOrigin origin)
    {
        if (AssignIfChanged(fillOrigin_, origin)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetFillClockwise(bool clockwise)
    {
        if (AssignIfChanged(fillClockwise_, clockwise)) {
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::RefreshTextureBorder()
    {
        const uint32_t revision = AssetDatabase::GetInstance().GetSliceBorderRevision();
        if (revision == textureBorderRevision_) {
            return;
        }
        textureBorderRevision_ = revision;

        Vector4 border = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (textureAsset_.IsSet()) {
            const std::array<float, 4> stored = AssetDatabase::GetInstance().GetSliceBorder(textureAsset_.GetPath());
            border = { stored[0], stored[1], stored[2], stored[3] };
        }
        if (border.x != textureBorder_.x || border.y != textureBorder_.y
            || border.z != textureBorder_.z || border.w != textureBorder_.w) {
            textureBorder_ = border;
            verticesDirty_ = true;
        }
    }

    bool UIImageComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    RectTransformComponent* UIImageComponent::GetRectTransform() const
    {
        return CachedSibling(rect_);
    }

    void UIImageComponent::GetFrameUv(Vector2& outMin, Vector2& outMax) const
    {
        // 今のコマの UV の範囲（範囲外のコマは端へ丸める）
        const int frame = std::clamp(frame_, 0, columns_ * rows_ - 1);
        const float cellWidth = 1.0f / static_cast<float>(columns_);
        const float cellHeight = 1.0f / static_cast<float>(rows_);
        outMin.x = cellWidth * static_cast<float>(frame % columns_);
        outMin.y = cellHeight * static_cast<float>(frame / columns_);
        outMax.x = outMin.x + cellWidth;
        outMax.y = outMin.y + cellHeight;
    }

    void UIImageComponent::WriteQuad(uint32_t quad, const Vector2& leftTop, const Vector2& rightBottom,
        const Vector2& uvLeftTop, const Vector2& uvRightBottom)
    {
        VertexData* const vertexData = vertices_.data() + static_cast<size_t>(quad) * 4;

        // 左下
        vertexData[0].position = { leftTop.x, rightBottom.y, 0.0f, 1.0f };
        vertexData[0].texcoord = { uvLeftTop.x, uvRightBottom.y };
        vertexData[0].normal = { 0.0f, 0.0f, -1.0f };

        // 左上
        vertexData[1].position = { leftTop.x, leftTop.y, 0.0f, 1.0f };
        vertexData[1].texcoord = { uvLeftTop.x, uvLeftTop.y };
        vertexData[1].normal = { 0.0f, 0.0f, -1.0f };

        // 右下
        vertexData[2].position = { rightBottom.x, rightBottom.y, 0.0f, 1.0f };
        vertexData[2].texcoord = { uvRightBottom.x, uvRightBottom.y };
        vertexData[2].normal = { 0.0f, 0.0f, -1.0f };

        // 右上
        vertexData[3].position = { rightBottom.x, leftTop.y, 0.0f, 1.0f };
        vertexData[3].texcoord = { uvRightBottom.x, uvLeftTop.y };
        vertexData[3].normal = { 0.0f, 0.0f, -1.0f };
    }

    void UIImageComponent::UpdateVertexData(const Vector2& pivot)
    {
        // 大きさ 1 の矩形を、基準点が原点に来るようにずらす（Y 下正）
        const float left = -pivot.x;
        const float right = 1.0f - pivot.x;
        const float top = -pivot.y;
        const float bottom = 1.0f - pivot.y;

        Vector2 uvMin;
        Vector2 uvMax;
        GetFrameUv(uvMin, uvMax);
        WriteQuad(0, { left, top }, { right, bottom }, uvMin, uvMax);
        quadCount_ = 1;

        builtPivot_ = pivot;
        verticesDirty_ = false;
    }

    void UIImageComponent::BuildSlicedVertices(const Vector2& pivot, const Vector2& size)
    {
        Vector2 uvMin;
        Vector2 uvMax;
        GetFrameUv(uvMin, uvMax);
        const Vector4 border = GetBorder();
        const float width = (std::max)(size.x, 0.0f);
        const float height = (std::max)(size.y, 0.0f);

        // 枠の幅の合計が大きさを上回るときは、角を同じ割合で縮めて重ならないようにする
        float left = border.x;
        float right = border.z;
        float top = border.y;
        float bottom = border.w;
        if (left + right > width && left + right > 0.0f) {
            const float shrink = width / (left + right);
            left *= shrink;
            right *= shrink;
        }
        if (top + bottom > height && top + bottom > 0.0f) {
            const float shrink = height / (top + bottom);
            top *= shrink;
            bottom *= shrink;
        }

        // 自分の座標（基準点が原点・px）の 4 本の縦線と横線
        const float x0 = -pivot.x * width;
        const float x3 = x0 + width;
        const float y0 = -pivot.y * height;
        const float y3 = y0 + height;
        const float xs[4] = { x0, x0 + left, x3 - right, x3 };
        const float ys[4] = { y0, y0 + top, y3 - bottom, y3 };

        // UV は伸ばす前の枠の幅で区切る
        const float texWidth = (std::max)(textureSize_.x, 1.0f);
        const float texHeight = (std::max)(textureSize_.y, 1.0f);
        const float us[4] = { uvMin.x, uvMin.x + border.x / texWidth, uvMax.x - border.z / texWidth, uvMax.x };
        const float vs[4] = { uvMin.y, uvMin.y + border.y / texHeight, uvMax.y - border.w / texHeight, uvMax.y };

        uint32_t quad = 0;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (row == 1 && column == 1 && !fillCenter_) {
                    continue;
                }
                WriteQuad(quad++, { xs[column], ys[row] }, { xs[column + 1], ys[row + 1] },
                    { us[column], vs[row] }, { us[column + 1], vs[row + 1] });
            }
        }
        quadCount_ = quad;

        builtPivot_ = pivot;
        builtSize_ = size;
        verticesDirty_ = false;
    }

    void UIImageComponent::BuildFilledVertices(const Vector2& pivot)
    {
        builtPivot_ = pivot;
        verticesDirty_ = false;

        const float amount = std::clamp(fillAmount_, 0.0f, 1.0f);
        if (amount <= 0.0f) {
            quadCount_ = 0;
            return;
        }

        Vector2 uvMin;
        Vector2 uvMax;
        GetFrameUv(uvMin, uvMax);

        // 大きさ 1 の四角の中の点（0,0 = 左上 / 1,1 = 右下）を、頂点の座標と UV にする
        const auto toLocal = [&](float u, float v) { return Vector2{ u - pivot.x, v - pivot.y }; };
        const auto toUv = [&](float u, float v) {
            return Vector2{ uvMin.x + (uvMax.x - uvMin.x) * u, uvMin.y + (uvMax.y - uvMin.y) * v };
        };

        if (fillMethod_ == UIFillMethod::Horizontal) {
            const bool fromRight = fillOrigin_ == UIFillOrigin::Right;
            const float from = fromRight ? 1.0f - amount : 0.0f;
            const float to = fromRight ? 1.0f : amount;
            WriteQuad(0, toLocal(from, 0.0f), toLocal(to, 1.0f), toUv(from, 0.0f), toUv(to, 1.0f));
            quadCount_ = 1;
            return;
        }
        if (fillMethod_ == UIFillMethod::Vertical) {
            const bool fromTop = fillOrigin_ == UIFillOrigin::Top;
            const float from = fromTop ? 0.0f : 1.0f - amount;
            const float to = fromTop ? amount : 1.0f;
            WriteQuad(0, toLocal(0.0f, from), toLocal(1.0f, to), toUv(0.0f, from), toUv(1.0f, to));
            quadCount_ = 1;
            return;
        }

        // 円：中心から、塗り始めの向き（12 時から時計回りの角度）へ扇を広げる
        constexpr float kDegrees = 3.14159265358979f / 180.0f;
        float start = 270.0f;
        switch (fillOrigin_) {
        case UIFillOrigin::Top:    start = 0.0f;   break;
        case UIFillOrigin::Right:  start = 90.0f;  break;
        case UIFillOrigin::Bottom: start = 180.0f; break;
        case UIFillOrigin::Left:   start = 270.0f; break;
        }
        const float sweep = amount * 360.0f;
        const float direction = fillClockwise_ ? 1.0f : -1.0f;

        // 角度の向きへ中心から伸ばし、四角の縁に当たった点
        const auto edgePoint = [&](float degrees) {
            const float radians = degrees * kDegrees;
            const float dx = std::sin(radians);
            const float dy = -std::cos(radians);
            const float reach = 0.5f / (std::max)(std::fabs(dx), std::fabs(dy));
            return Vector2{ 0.5f + dx * reach, 0.5f + dy * reach };
        };

        // 扇の縁：始まりの点・間にある四隅・終わりの点
        struct EdgeStop { float t; Vector2 point; };
        EdgeStop stops[6];
        int stopCount = 0;
        stops[stopCount++] = { 0.0f, edgePoint(start) };
        const struct { float angle; Vector2 point; } corners[4] = {
            { 45.0f,  { 1.0f, 0.0f } },
            { 135.0f, { 1.0f, 1.0f } },
            { 225.0f, { 0.0f, 1.0f } },
            { 315.0f, { 0.0f, 0.0f } },
        };
        for (const auto& corner : corners) {
            float t = std::fmod(direction * (corner.angle - start) + 720.0f, 360.0f);
            if (t > 0.0f && t < sweep) {
                stops[stopCount++] = { t, corner.point };
            }
        }
        std::sort(stops + 1, stops + stopCount, [](const EdgeStop& a, const EdgeStop& b) { return a.t < b.t; });
        stops[stopCount++] = { sweep, edgePoint(start + direction * sweep) };

        // 中心と縁の 2 点で三角形を作る（四角の右上を右下に重ねて三角形にする）
        const Vector2 center = { 0.5f, 0.5f };
        uint32_t quad = 0;
        for (int i = 0; i + 1 < stopCount && quad < kMaxQuads; ++i) {
            const Vector2 a = stops[i].point;
            const Vector2 b = stops[i + 1].point;
            VertexData* const vertexData = vertices_.data() + static_cast<size_t>(quad) * 4;
            const Vector2 points[4] = { center, a, b, b };
            for (int k = 0; k < 4; ++k) {
                const Vector2 local = toLocal(points[k].x, points[k].y);
                const Vector2 uv = toUv(points[k].x, points[k].y);
                vertexData[k].position = { local.x, local.y, 0.0f, 1.0f };
                vertexData[k].texcoord = { uv.x, uv.y };
                vertexData[k].normal = { 0.0f, 0.0f, -1.0f };
            }
            ++quad;
        }
        quadCount_ = quad;
    }

    void UIImageComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        if (!owner || !owner->IsActive()) { return; }

        // テクスチャが無いまま SRV を差すとデバッグレイヤーが止めるので描かない
        ID3D12GraphicsCommandList* const commandList = view.cmdList;
        const RectTransformComponent* const rect = GetRectTransform();
        if (!commandList || !renderer_ || !material_ || !rect || textureHandle_.gpuHandle.ptr == 0) {
            return;
        }

        // 親が隠れていれば描かない。マスクの絵を描かない設定なら切る範囲としてだけ使う
        if (!rect->IsActiveInHierarchy()) { return; }
        if (const UIMaskComponent* const mask = owner->GetComponent<UIMaskComponent>();
            mask && mask->IsEnabled() && !mask->IsShowMaskGraphic()) {
            return;
        }

        const Vector2 canvasSize = renderer_->GetScreenSize();
        const UILayout& layout = rect->GetLayout();
        const Vector2 size = rect->GetRectSize(canvasSize);
        const bool pivotChanged = layout.pivot.x != builtPivot_.x || layout.pivot.y != builtPivot_.y;

        // 単純と塗る量は大きさ 1 の四角を大きさで伸ばし、9 スライスは px で組んで伸ばさない
        Vector2 contentScale = size;
        switch (imageType_) {
        case UIImageType::Simple:
            if (verticesDirty_ || pivotChanged) {
                UpdateVertexData(layout.pivot);
            }
            break;
        case UIImageType::Sliced:
            RefreshTextureBorder();
            if (verticesDirty_ || pivotChanged || size.x != builtSize_.x || size.y != builtSize_.y) {
                BuildSlicedVertices(layout.pivot, size);
            }
            contentScale = { 1.0f, 1.0f };
            break;
        case UIImageType::Filled:
            if (verticesDirty_ || pivotChanged) {
                BuildFilledVertices(layout.pivot);
            }
            break;
        }
        if (quadCount_ == 0) { return; }

        UploadRing& uploadRing = renderer_->GetGraphicsCore()->GetUploadRing();

        // 頂点は描くたびに、このフレームの UploadRing に置く
        const uint32_t vertexBytes = static_cast<uint32_t>(sizeof(VertexData) * 4 * quadCount_);
        const UploadAllocation vertices = uploadRing.Allocate(vertexBytes, 16);
        if (!vertices.IsValid()) { return; }
        std::memcpy(vertices.cpu, vertices_.data(), vertexBytes);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = vertices.gpuAddress;
        vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
        vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));

        const Matrix4x4 world = rect->BuildDrawMatrix(contentScale, canvasSize);

        const size_t bufferIndex = renderer_->GetAvailableConstantBuffer();
        UIRenderer::TransformationMatrix* transformData = renderer_->GetTransformData(bufferIndex);
        transformData->WVP = renderer_->CalculateWVPFromWorld(world);
        transformData->world = world;
        const D3D12_GPU_VIRTUAL_ADDRESS transformAddress = renderer_->GetTransformGpuAddress(bufferIndex);

        // 親の UI マスクの外は描かない
        UIRect clip;
        const bool clipped = rect->FindClipRect(canvasSize, clip);
        renderer_->ApplyClip(commandList, clipped ? &clip : nullptr);

        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        commandList->IASetIndexBuffer(&renderer_->GetQuadIndexBufferView());
        const UINT indexCount = 6 * quadCount_;

        if (materialSlot_.IsSet() && RenderWithMaterial(commandList, uploadRing, transformAddress)) {
            commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
            return;
        }

        renderer_->BindPipeline(commandList, nullptr);

        // マテリアルを指しているのに描けないときは、気づけるようマゼンタで描く
        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = material_->GetGPUVirtualAddress();
        if (materialSlot_.IsSet()) {
            SpriteMaterialData fallback{};
            fallback.color = { 1.0f, 0.0f, 1.0f, 1.0f };
            fallback.uvTransform = Matrix::Identity();
            materialAddress = uploadRing.AllocateConstants(fallback);
            if (materialAddress == 0) { return; }
        }

        const BindingTable& bindings = renderer_->GetBindings();
        ShaderBinder binder(commandList, ShaderBinder::Pipeline::Graphics);
        binder.Set(bindings[UIBind::gMaterial], materialAddress);
        binder.Set(bindings[UIBind::TransformationMatrix], transformAddress);
        binder.Set(bindings[UIBind::gTexture], textureHandle_.gpuHandle);
        binder.ValidateBeforeDraw(bindings);

        commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
    }

    bool UIImageComponent::RenderWithMaterial(ID3D12GraphicsCommandList* commandList, UploadRing& uploadRing,
        D3D12_GPU_VIRTUAL_ADDRESS transformAddress)
    {
        const ShaderMaterial* const material = materialSlot_.GetMaterial();
        const ShaderMaterialPipeline* const pipeline = material ? renderer_->GetMaterialPipeline(*material) : nullptr;
        if (!pipeline) {
            return false;
        }

        SpriteDrawData draw{};
        draw.color = color_;
        draw.textureSize = textureSize_;
        draw.time = Time::TimeSinceStartup();
        draw.unscaledTime = Time::UnscaledTimeSinceStartup();
        draw.deltaTime = Time::DeltaTime();
        const D3D12_GPU_VIRTUAL_ADDRESS drawAddress = uploadRing.AllocateConstants(draw);
        if (drawAddress == 0) {
            return false;
        }

        renderer_->BindPipeline(commandList, pipeline);
        if (!materialSlot_.Bind(commandList, *pipeline, uploadRing, transformAddress, drawAddress, textureHandle_.gpuHandle)) {
            renderer_->BindPipeline(commandList, nullptr);
            return false;
        }
        return true;
    }
}
