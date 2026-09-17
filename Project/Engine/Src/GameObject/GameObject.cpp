#include "pch.h"
#include "GameObject.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Collision/ColliderComponent.h"
#include "Graphics/Render/DrawViewInfo.h"
#include <cstdio>


namespace CoreEngine
{
    namespace {
        EngineSystem* sEngine = nullptr;

    }

    // ===== ライフサイクル =====

    void GameObject::SetEngine(EngineSystem* engine) {
        if (sEngine == nullptr) {
            sEngine = engine;
        }
    }

    EngineSystem* GameObject::GetEngineSystem() const {
        return sEngine;
    }

    void GameObject::Draw(const DrawViewInfo& view) {
        // 描画はコンポーネントが答える
        for (const auto& slot : GetAllComponents()) {
            if (!slot || !slot->IsEnabled()) { continue; }
            if (auto* renderable = dynamic_cast<IRenderableComponent*>(slot.get())) {
                renderable->Render(view);
            }
        }
    }

    // ===== トランスフォーム（コンポーネントへの委譲） =====

    Vector3 GameObject::GetWorldPosition() const {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->GetWorldPosition();   // 親の階層を含むワールド位置
        }
        if (auto* source = GetComponent<ITransformSource>()) {
            return const_cast<ITransformSource*>(source)->Translate();
        }
        return {};
    }

    Vector3 GameObject::GetWorldScale() const {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->GetWorldScale();      // 親の階層スケールを含む
        }
        if (auto* source = GetComponent<ITransformSource>()) {
            return const_cast<ITransformSource*>(source)->Scale();
        }
        return { 1.0f, 1.0f, 1.0f };
    }

    bool GameObject::TryApplyCollisionPush(const Vector3& delta) {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->ApplyWorldDelta(delta);
        }
        return false;
    }

    // ===== アクティブ =====

    void GameObject::SetActive(bool active) { isActive_ = active; }
    bool GameObject::IsActive() const { return isActive_; }

    // ===== 破棄 =====

    void GameObject::Destroy() { markedForDestroy_ = true; }
    bool GameObject::IsMarkedForDestroy() const { return markedForDestroy_; }

    // ===== 描画制御 =====

    void GameObject::SetRenderOrder(int order) { renderOrder_ = order; }
    std::optional<int> GameObject::GetRenderOrder() const { return renderOrder_; }
    void GameObject::ResetRenderOrder() { renderOrder_ = std::nullopt; }

    // 描画パス・ブレンドは描画コンポーネントが答える（無ければ既定値）

    RenderPassType GameObject::GetRenderPassType() const {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            return renderable->GetRenderPassType();
        }
        return RenderPassType::Model;
    }

    BlendMode GameObject::GetBlendMode() const {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            return renderable->GetBlendMode();
        }
        return BlendMode::kBlendModeNone;
    }

    void GameObject::SetBlendMode(BlendMode blendMode) {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            renderable->SetBlendMode(blendMode);
        }
    }

    RenderItem GameObject::BuildRenderItem() const {
        RenderItem item;
        item.object = const_cast<GameObject*>(this);
        item.passType = GetRenderPassType();
        item.blendMode = GetBlendMode();
        item.renderOrderOverride = GetRenderOrder();

        // 描画コンポーネントが種別を明示していればそれを尊重する
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            item.kind = renderable->GetRenderItemKind();
        }

        if (item.kind == RenderItemKind::Default) {
            if (item.passType == RenderPassType::SkyBox) {
                item.kind = RenderItemKind::SkyBox;
            } else if (item.passType == RenderPassType::WaterSurface) {
                item.kind = RenderItemKind::WaterSurface;
            } else if (item.blendMode != BlendMode::kBlendModeNone) {
                item.kind = RenderItemKind::Transparent;
            }
        }

        return item;
    }

    // ===== 衝突イベント =====

    // 接触の通知はコライダーの購読者へ配る（継承して受け取る口は持たない）

    void GameObject::NotifyCollisionEnter(const CollisionInfo& info) {
        if (auto* colliders = GetComponent<ColliderComponent>()) { colliders->DispatchEnter(info); }
    }
    void GameObject::NotifyCollisionStay(const CollisionInfo& info) {
        if (auto* colliders = GetComponent<ColliderComponent>()) { colliders->DispatchStay(info); }
    }
    void GameObject::NotifyCollisionExit(const CollisionInfo& info) {
        if (auto* colliders = GetComponent<ColliderComponent>()) { colliders->DispatchExit(info); }
    }

    // ===== 名前 / シリアライズ =====

    void GameObject::SetName(const std::string& name) {
        name_ = name;
        if (serializeKey_.empty()) {
            serializeKey_ = name;
        }
    }
    const std::string& GameObject::GetName() const { return name_; }
    const std::string& GameObject::GetSerializeKey() const { return serializeKey_; }

    const char* GameObject::GetDisplayName() const {
        if (!name_.empty()) return name_.c_str();
        if (!serializeKey_.empty()) return serializeKey_.c_str();
        return "GameObject";
    }

    bool GameObject::IsSerializeEnabled() const { return shouldSerialize_; }
    void GameObject::SetSerializeEnabled(bool enable) { shouldSerialize_ = enable; }

    json GameObject::Serialize() const
    {
        json j = json::object();
        j["active"] = IsActive();
        if (!name_.empty()) {
            j["name"] = name_;
        }

        json components = SerializeComponents();
        if (!components.empty()) {
            j["components"] = std::move(components);
        }
        return j;
    }

    void GameObject::Deserialize(const json& j)
    {
        if (!j.is_object()) {
            return;
        }

        if (j.contains("active") && j["active"].is_boolean()) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("name") && j["name"].is_string()) {
            name_ = j["name"].get<std::string>();
        }
        if (j.contains("components")) {
            DeserializeComponents(j["components"]);
        }
    }


}


