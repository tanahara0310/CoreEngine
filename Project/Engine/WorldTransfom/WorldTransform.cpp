#include "WorldTransform.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include <cassert>
#include <cmath>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

void WorldTransform::Initialize(ID3D12Device* device)
{
    // 定数バッファを作成
    constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(ConstantBufferDataWorldTransform));

    // マッピング
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_));
    assert(mapped_ != nullptr);

    // 初期行列を転送
    TransferMatrix();
}

void WorldTransform::TransferMatrix()
{
    // ローカル行列を計算（回転モードに応じて処理を分岐）
    Matrix4x4 localMatrix;
    
    if (rotationMode_ == RotationMode::Quaternion) {
        // クォータニオン回転モード
        localMatrix = Matrix::MakeAffine(scale, quaternionRotate, translate);
    } else {
        // オイラー角回転モード
        localMatrix = Matrix::MakeAffine(scale, rotate, translate);
    }

    // 親がいる場合は親の行列と合成
    if (parent_) {
        matWorld_ = Matrix::Multiply(localMatrix, parent_->GetWorldMatrix());
    } else {
        matWorld_ = localMatrix;
    }

    // GPUに転送
    if (mapped_) {
        mapped_->matWorld = matWorld_;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS WorldTransform::GetGPUVirtualAddress() const
{
    return constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
}

Vector3 WorldTransform::GetWorldPosition() const
{
    return { matWorld_.m[3][0], matWorld_.m[3][1], matWorld_.m[3][2] };
}

void WorldTransform::SetWorldMatrix(const Matrix4x4& matrix)
{
    matWorld_ = matrix;
    
    // GPUに転送
    if (mapped_) {
        mapped_->matWorld = matWorld_;
    }
}

void WorldTransform::EulerToQuaternion()
{
    // X軸回転
    Quaternion qx = QuaternionMath::MakeRotateAxisAngle({1.0f, 0.0f, 0.0f}, rotate.x);
    // Y軸回転
    Quaternion qy = QuaternionMath::MakeRotateAxisAngle({0.0f, 1.0f, 0.0f}, rotate.y);
    // Z軸回転
    Quaternion qz = QuaternionMath::MakeRotateAxisAngle({0.0f, 0.0f, 1.0f}, rotate.z);
    
    // 回転の合成（Z * X * Y の順）
    quaternionRotate = QuaternionMath::Multiply(QuaternionMath::Multiply(qz, qx), qy);
}

void WorldTransform::QuaternionToEuler()
{
    // クォータニオンから回転行列を生成
    Matrix4x4 rotMatrix = QuaternionMath::MakeRotateMatrix(quaternionRotate);
    
    // 回転行列からオイラー角を抽出
    // ジンバルロックを考慮したオイラー角抽出
    float sy = std::sqrt(rotMatrix.m[0][0] * rotMatrix.m[0][0] + rotMatrix.m[1][0] * rotMatrix.m[1][0]);
    
    bool singular = sy < 1e-6f;
    
    if (!singular) {
        rotate.x = std::atan2(rotMatrix.m[2][1], rotMatrix.m[2][2]);
        rotate.y = std::atan2(-rotMatrix.m[2][0], sy);
        rotate.z = std::atan2(rotMatrix.m[1][0], rotMatrix.m[0][0]);
    } else {
        rotate.x = std::atan2(-rotMatrix.m[1][2], rotMatrix.m[1][1]);
        rotate.y = std::atan2(-rotMatrix.m[2][0], sy);
        rotate.z = 0.0f;
    }
}

#ifdef _DEBUG
bool WorldTransform::DrawImGui(const std::string& label)
{
    bool changed = false;

    if (ImGui::CollapsingHeader((label + " Transform").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        // 回転モード選択
        const char* rotationModes[] = { "オイラー角", "クォータニオン" };
        int currentMode = static_cast<int>(rotationMode_);
        if (ImGui::Combo((label + " 回転モード").c_str(), &currentMode, rotationModes, 2)) {
            RotationMode newMode = static_cast<RotationMode>(currentMode);
            
            // モード変更時に回転値を変換
            if (newMode != rotationMode_) {
                if (newMode == RotationMode::Quaternion) {
                    EulerToQuaternion();
                } else {
                    QuaternionToEuler();
                }
                rotationMode_ = newMode;
                changed = true;
            }
        }
        
        // Scale
        if (ImGui::DragFloat3((label + " スケール").c_str(), &scale.x, 0.01f, 0.001f, 10.0f)) {
            changed = true;
        }

        // Rotate（回転モードに応じて表示を切り替え）
        if (rotationMode_ == RotationMode::Euler) {
            // オイラー角表示
            if (ImGui::DragFloat3((label + " 回転").c_str(), &rotate.x, 0.01f, -6.28f, 6.28f)) {
                changed = true;
            }
        } else {
            // クォータニオン表示
            ImGui::Text("クォータニオン回転:");
            if (ImGui::DragFloat4((label + " Quaternion").c_str(), &quaternionRotate.x, 0.01f, -1.0f, 1.0f)) {
                // 正規化
                quaternionRotate = QuaternionMath::Normalize(quaternionRotate);
                changed = true;
            }
            
            // 参考用にオイラー角も表示（読み取り専用）
            ImGui::BeginDisabled();
            Vector3 eulerDisplay = rotate;
            QuaternionToEuler();
            eulerDisplay = rotate;
            ImGui::DragFloat3((label + " (オイラー角参考)").c_str(), &eulerDisplay.x, 0.01f);
            ImGui::EndDisabled();
        }

        // Translate
        if (ImGui::DragFloat3((label + " 位置").c_str(), &translate.x, 0.05f, -100.0f, 100.0f)) {
            changed = true;
        }

        // 親の情報
        if (parent_) {
            ImGui::Text("親: あり");
            Vector3 worldPos = GetWorldPosition();
            ImGui::Text("ワールド座標: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);
        } else {
            ImGui::Text("親: なし");
        }

        // リセットボタン
        if (ImGui::Button((label + " リセット").c_str())) {
            scale = { 1.0f, 1.0f, 1.0f };
            rotate = { 0.0f, 0.0f, 0.0f };
            translate = { 0.0f, 0.0f, 0.0f };
            quaternionRotate = { 0.0f, 0.0f, 0.0f, 1.0f };
            changed = true;
        }
    }

    return changed;
}
#else
bool WorldTransform::DrawImGui(const std::string& label)
{
    (void)label;
    return false;
}
#endif
}
