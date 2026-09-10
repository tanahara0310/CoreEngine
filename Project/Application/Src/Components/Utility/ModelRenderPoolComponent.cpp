#include "pch.h"
#include "ModelRenderPoolComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace {
    /// @brief 位置から安定した割り当てキーを作る
    /// @details Y は演出（レール確定時の跳ね上げなど）で毎フレーム変わりうるので使わない。
    ///          XZ を 0.01 単位へ量子化して 64bit へ詰める。マップ 1 マスは
    ///          必ず同じ座標で Draw されるので、これで毎フレーム同じキーになる。
    std::uint64_t MakePositionKey(const Vector3& position) {
        const auto x = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::llround(position.x * 100.0)));
        const auto z = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::llround(position.z * 100.0)));
        return (static_cast<std::uint64_t>(x) << 32) | static_cast<std::uint64_t>(z);
    }
}

json GameComponents::ModelRenderPoolComponent::OnSerialize() const {
    json result = {
        { "initialCapacity", initialCapacity_ },
        { "allowGrowth", allowGrowth_ },
        { "hasColorOverride", color_.has_value() }
    };
    if (color_) {
        result["color"] = JsonManager::Vector4ToJson(*color_);
    }
    return result;
}

void GameComponents::ModelRenderPoolComponent::OnDeserialize(const json& j) {
    const std::size_t capacity = std::max<std::size_t>(1,
        JsonManager::SafeGet<std::size_t>(j, "initialCapacity", initialCapacity_));
    allowGrowth_ = JsonManager::SafeGet<bool>(j, "allowGrowth", allowGrowth_);
    const bool hasColor = JsonManager::SafeGet<bool>(j, "hasColorOverride", color_.has_value());
    if (hasColor) {
        color_ = JsonManager::SafeGetVector4(j, "color", color_.value_or(Vector4{ 1, 1, 1, 1 }));
    } else {
        color_.reset();
    }
    ResizePool(capacity);
    ApplyColorToEntries();
}

#ifdef USE_IMGUI
bool GameComponents::ModelRenderPoolComponent::DrawInspector() {
    bool changed = false;
    ImGui::TextDisabled("モデル: %s", modelPath_.c_str());
    int capacity = static_cast<int>(initialCapacity_);
    if (ImGui::DragInt("プール容量", &capacity, 1.0f, 1, 5000)) {
        ResizePool(static_cast<std::size_t>(std::max(capacity, 1)));
        changed = true;
    }
    changed |= ImGui::Checkbox("容量不足時に拡張", &allowGrowth_);

    bool hasColor = color_.has_value();
    if (ImGui::Checkbox("色を上書き", &hasColor)) {
        if (hasColor) color_ = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        else color_.reset();
        ApplyColorToEntries();
        changed = true;
    }
    if (color_) {
        Vector4 edited = *color_;
        if (ImGui::ColorEdit4("色", &edited.x)) {
            color_ = edited;
            ApplyColorToEntries();
            changed = true;
        }
    }
    ImGui::TextDisabled("使用中: %zu / %zu", GetActiveCount(), GetCapacity());
    return changed;
}
#endif

void GameComponents::ModelRenderPoolComponent::Awake() {
    if (modelPath_.empty()) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "ModelRenderPoolComponent: モデルパスが空です");
        SetEnabled(false);
        return;
    }

    entries_.reserve(initialCapacity_);
    while (entries_.size() < initialCapacity_) {
        if (!CreateEntry()) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "ModelRenderPoolComponent: プールの事前生成に失敗しました ({}/{})",
                entries_.size(), initialCapacity_);
            SetEnabled(false);
            return;
        }
    }
}

void GameComponents::ModelRenderPoolComponent::Update() {
    const std::uint64_t frame = Time::FrameCount();
    for (Entry& entry : entries_) {
        if (!entry.object || entry.object->IsMarkedForDestroy()) {
            continue;
        }

        // 呼び出し元がこの Update より先に Draw() していても、同じフレームなら残す。
        if (entry.lastSubmittedFrame != frame && entry.object->IsActive()) {
            entry.object->SetActive(false);
        }
    }
}

void GameComponents::ModelRenderPoolComponent::OnDestroy() {
    for (Entry& entry : entries_) {
        if (entry.object && !entry.object->IsMarkedForDestroy()) {
            entry.object->Destroy();
        }
    }
    entries_.clear();
}

bool GameComponents::ModelRenderPoolComponent::Draw(
    const Vector3& position,
    const Vector3& rotation,
    const Vector3& scale,
    const std::optional<Vector4>& color) {
    const std::uint64_t frame = Time::FrameCount();
    BeginFrameIfNeeded(frame);

    // 前フレームに同じ場所を描いた要素を優先して使い回す。
    // 呼び出し順で先頭から配ると、カメラが 1 マス進んで描画範囲がずれた瞬間に
    // 全要素の担当マスが 1 つずつずれる。画面上の地形は静止して見えるのに
    // オブジェクトだけが 1 マス飛ぶので、GBuffer のモーションベクターが
    // 「動いた」と嘘をつき、RT シャドウのテンポラル再投影が 1 マスずれた履歴を
    // 拾ってしまう（＝プレイヤーが動いている間だけ影がちらつく）。
    const std::uint64_t positionKey = MakePositionKey(position);
    Entry* entry = FindEntryForPosition(positionKey, frame);
    if (!entry) {
        entry = FindAvailableEntry(frame);
    }
    if (!entry && allowGrowth_) {
        entry = CreateEntry();
    }

    if (!entry || !entry->object || !entry->transform) {
        // 固定容量を超えた場合でも、警告は1フレームに1回までに抑える。
        if (lastExhaustedWarningFrame_ != frame) {
            lastExhaustedWarningFrame_ = frame;
            Logger::GetInstance().Warnf(
                LogCategory::Game,
                "ModelRenderPoolComponent: 1フレームの描画数が容量を超えました (capacity={})",
                entries_.size());
        }
        return false;
    }

    ApplyEntryColor(*entry, color);

    auto& transform = entry->transform->Get();
    transform.translate = position;
    transform.rotate = rotation;
    transform.scale = scale;
    transform.TransferMatrix();

    entry->lastSubmittedFrame = frame;
    entry->object->SetActive(true);
    entryByPosition_[positionKey] =
        static_cast<std::size_t>(entry - entries_.data());
    return true;
}

std::size_t GameComponents::ModelRenderPoolComponent::GetActiveCount() const {
    const std::uint64_t frame = Time::FrameCount();
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(), entries_.end(),
        [frame](const Entry& entry) {
            return entry.lastSubmittedFrame == frame;
        }));
}

GameComponents::ModelRenderPoolComponent::Entry*
GameComponents::ModelRenderPoolComponent::CreateEntry() {
    GameObject* owner = GetOwner();
    if (!owner) {
        return nullptr;
    }

    GameObject* object = owner->Spawn<GameObject>();
    if (!object) {
        return nullptr;
    }

    object->SetName(
        owner->GetName() + "_PooledModel_" + std::to_string(entries_.size()));
    object->SetSerializeEnabled(false);

    TransformComponent* transform = object->AddComponent<TransformComponent>();
    object->AddComponent<MeshRendererComponent>(modelPath_);
    MaterialComponent* material = nullptr;
    if (color_) {
        material = object->AddComponent<MaterialComponent>();
        material->SetColor(*color_);
        material->SetPBR(0.0f, 0.15f);
    }
    object->SetActive(false);

    entries_.push_back({ object, transform, material, color_ });
    return &entries_.back();
}

void GameComponents::ModelRenderPoolComponent::ApplyEntryColor(
    Entry& entry, const std::optional<Vector4>& color) {
    // Draw() が色を指定しないときは、プール共通の色（未設定なら白）へ戻す。
    // 要素は別のマスへ再割り当てされるので、前のマスの色を残さない。
    const std::optional<Vector4> desired = color ? color : color_;
    if (!desired && !entry.appliedColor) {
        // 一度も色を付けていない要素は、モデル本来のマテリアルのまま触らない。
        return;
    }
    const Vector4 next = desired.value_or(Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
    if (entry.appliedColor && entry.appliedColor->x == next.x &&
        entry.appliedColor->y == next.y && entry.appliedColor->z == next.z &&
        entry.appliedColor->w == next.w) {
        return;
    }

    if (!entry.material && entry.object) {
        entry.material = entry.object->GetComponent<MaterialComponent>();
        if (!entry.material) {
            // 色だけ差し替える。PBR ファクターはモデルの持ち物なので触らない。
            entry.material = entry.object->AddComponent<MaterialComponent>();
        }
    }
    if (!entry.material) {
        return;
    }
    entry.material->SetColor(next);
    entry.appliedColor = next;
}

void GameComponents::ModelRenderPoolComponent::BeginFrameIfNeeded(std::uint64_t frame) {
    if (allocationFrame_ == frame) {
        return;
    }
    allocationFrame_ = frame;
    nextEntryIndex_ = 0;
    prevEntryByPosition_ = std::move(entryByPosition_);
    entryByPosition_.clear();
}

GameComponents::ModelRenderPoolComponent::Entry*
GameComponents::ModelRenderPoolComponent::FindEntryForPosition(
    std::uint64_t positionKey, std::uint64_t frame) {
    const auto it = prevEntryByPosition_.find(positionKey);
    if (it == prevEntryByPosition_.end() || it->second >= entries_.size()) {
        return nullptr;
    }

    Entry& entry = entries_[it->second];
    // 既に今フレーム使われている（＝同じ場所へ二重に Draw された）なら諦める
    if (entry.lastSubmittedFrame == frame) {
        return nullptr;
    }
    if (!entry.object || entry.object->IsMarkedForDestroy()) {
        return nullptr;
    }
    return &entry;
}

GameComponents::ModelRenderPoolComponent::Entry*
GameComponents::ModelRenderPoolComponent::FindAvailableEntry(std::uint64_t frame) {
    while (nextEntryIndex_ < entries_.size()) {
        Entry& entry = entries_[nextEntryIndex_++];
        if (entry.lastSubmittedFrame != frame && entry.object &&
            !entry.object->IsMarkedForDestroy()) {
            return &entry;
        }
    }
    return nullptr;
}

void GameComponents::ModelRenderPoolComponent::ResizePool(std::size_t capacity) {
    initialCapacity_ = std::max<std::size_t>(1, capacity);
    entries_.reserve(initialCapacity_);
    while (entries_.size() < initialCapacity_) {
        if (!CreateEntry()) break;
    }
    while (entries_.size() > initialCapacity_) {
        Entry& entry = entries_.back();
        if (entry.object && !entry.object->IsMarkedForDestroy()) {
            entry.object->Destroy();
        }
        entries_.pop_back();
    }
    nextEntryIndex_ = std::min(nextEntryIndex_, entries_.size());
    // 添字が指す先が変わるので、位置キーの対応付けは作り直す
    entryByPosition_.clear();
    prevEntryByPosition_.clear();
}

void GameComponents::ModelRenderPoolComponent::ApplyColorToEntries() {
    for (Entry& entry : entries_) {
        if (!entry.object) continue;
        if (auto* existingMaterial = entry.object->GetComponent<MaterialComponent>()) {
            entry.material = existingMaterial;
            entry.appliedColor = color_.value_or(Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
            existingMaterial->SetColor(*entry.appliedColor);
        } else if (color_) {
            auto* newMaterial = entry.object->AddComponent<MaterialComponent>();
            newMaterial->SetColor(*color_);
            newMaterial->SetPBR(0.0f, 0.15f);
            entry.material = newMaterial;
            entry.appliedColor = color_;
        }
    }
}
