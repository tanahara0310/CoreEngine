#include "pch.h"
#include "TransformComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Math/MathCore.h"

#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    void TransformComponent::Awake()
    {
        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        if (auto* dxCommon = engine ? engine->GetService<GraphicsCore>() : nullptr) {
            transform_.Initialize(dxCommon->GetDevice());
        }
    }

    Vector3 TransformComponent::GetWorldScale() const
    {
        // 行ベクトル規約（p' = p * M）なので、各行が基底ベクトル。その長さがスケール。
        const auto& m = transform_.GetWorldMatrix().m;
        auto axisLength = [&m](int row) {
            return std::sqrt(m[row][0] * m[row][0] + m[row][1] * m[row][1] + m[row][2] * m[row][2]);
            };
        return { axisLength(0), axisLength(1), axisLength(2) };
    }

    bool TransformComponent::ApplyWorldDelta(const Vector3& delta)
    {
        if (const WorldTransform* parent = transform_.GetParent()) {
            // 親がいる場合、delta はワールド量なので親のローカル空間へ落としてから足す。
            // delta は「向きと大きさ」を持つベクトルなので平行移動成分は掛けない
            // （TransformNormal が w=0 として扱う）。
            const Matrix4x4 parentInverse =
                MathCore::Matrix::Inverse(parent->GetWorldMatrix());
            transform_.translate =
                transform_.translate
                + MathCore::CoordinateTransform::TransformNormal(delta, parentInverse);
        } else {
            transform_.translate = transform_.translate + delta;
        }

        // 同一フレーム内の後続ペアが新しい位置で判定されるようワールド行列を更新する
        transform_.TransferMatrix();
        return true;
    }

#ifdef USE_IMGUI
    bool TransformComponent::DrawInspector()
    {
        bool changed = false;

        // ドラッグを始めた時点の値を控えておき、離した瞬間に Undo へ積む。
        // 毎フレーム積むとドラッグ 1 回で履歴が数十件生えてしまう
        auto captureSnapshot = [this]() {
            editSnapTranslate_ = transform_.translate;
            editSnapRotate_ = transform_.rotate;
            editSnapScale_ = transform_.scale;
            editSnapActive_ = GetOwner() ? GetOwner()->IsActive() : true;
            };

        auto commitIfFinished = [this]() {
            if (ImGui::IsItemDeactivatedAfterEdit() && GetOwner()) {
                GetOwner()->NotifyEditCommitted(
                    editSnapTranslate_, editSnapRotate_, editSnapScale_, editSnapActive_);
            }
            };

        if (UI::DragVec3("位置", transform_.translate, 0.05f)) { changed = true; }
        if (ImGui::IsItemActivated()) { captureSnapshot(); }
        commitIfFinished();

        if (UI::DragVec3("回転", transform_.rotate, 0.01f)) { changed = true; }
        if (ImGui::IsItemActivated()) { captureSnapshot(); }
        commitIfFinished();

        if (UI::DragVec3("スケール", transform_.scale, 0.01f)) { changed = true; }
        if (ImGui::IsItemActivated()) { captureSnapshot(); }
        commitIfFinished();

        if (changed) {
            // ギズモ・当たり判定が同じフレームで新しい値を見られるようにする
            transform_.TransferMatrix();
        }

        UI::Separator();
        const Vector3 worldPos = GetWorldPosition();
        UI::Hint("回転はラジアン");
        ImGui::Text("ワールド位置: %.3f, %.3f, %.3f", worldPos.x, worldPos.y, worldPos.z);

        return changed;
    }
#endif // USE_IMGUI
}
