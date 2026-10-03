#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <string>

namespace CoreEngine
{
// 定数バッファ用データ
struct ConstantBufferDataWorldTransform {
    Matrix4x4 matWorld; // ワールド変換行列
};

static constexpr Cb::Field kWorldTransformFields[] = {
    CB_FIELD(ConstantBufferDataWorldTransform, matWorld),
};
CB_VERIFY_LAYOUT(ConstantBufferDataWorldTransform, kWorldTransformFields);

/// <summary>
/// ワールドトランスフォームクラス
/// 3Dオブジェクトの位置・回転・スケールを管理し、GPU用の行列を生成する
/// </summary>
class WorldTransform {
public:
    // ===== ローカルの位置・回転・スケール =====

    /// @brief 位置
    const Vector3& GetTranslate() const { return translate_; }
    void SetTranslate(const Vector3& translate) { translate_ = translate; }

    /// @brief スケール
    const Vector3& GetScale() const { return scale_; }
    void SetScale(const Vector3& scale) { scale_ = scale; }

    /// @brief 回転（ワールド行列はこれから作る）
    const Quaternion& GetRotation() const { return rotation_; }

    /// @brief 回転を設定し、オイラー角の写しも作り直す
    void SetRotation(const Quaternion& rotation);

    /// @brief 回転のオイラー角（ラジアン。X → Y → Z の順に回す。表示と保存のための写し）
    const Vector3& GetRotationEuler() const { return rotationEuler_; }

    /// @brief オイラー角（ラジアン）で回転を設定する（渡した角度をそのまま写しとして持つ）
    void SetRotationEuler(const Vector3& radians);

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="device">D3D12デバイス</param>
    void Initialize(ID3D12Device* device);

    /// <summary>
    /// ワールド行列を計算してGPUに転送
    /// 毎フレーム描画前に呼び出す
    /// </summary>
    void TransferMatrix();

    /// <summary>
    /// GPU仮想アドレスを取得
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

    /// <summary>
    /// 計算済みワールド行列を取得
    /// </summary>
    const Matrix4x4& GetWorldMatrix() const { return matWorld_; }

    /// <summary>
    /// ワールド座標での位置を取得
    /// </summary>
    Vector3 GetWorldPosition() const;

    /// <summary>
    /// 親トランスフォームを設定（階層構造用）
    /// </summary>
    /// <param name="parent">親トランスフォームのポインタ（nullptrで親なし）</param>
    void SetParent(const WorldTransform* parent) { parent_ = parent; }

    /// <summary>
    /// 親トランスフォームを取得
    /// </summary>
    const WorldTransform* GetParent() const { return parent_; }

    /// <summary>
    /// ワールド行列を直接設定（アニメーション用）
    /// </summary>
    /// <param name="matrix">設定する行列</param>
    void SetWorldMatrix(const Matrix4x4& matrix);

private:
    // 定数バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    // マッピング済みポインタ
    ConstantBufferDataWorldTransform* mapped_ = nullptr;
    // 計算済みワールド行列
    Matrix4x4 matWorld_;
    // 親トランスフォーム（階層構造用）
    const WorldTransform* parent_ = nullptr;
    // ローカルの位置・回転・スケール
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };
    Quaternion rotation_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vector3 rotationEuler_ = { 0.0f, 0.0f, 0.0f };
    Vector3 translate_ = { 0.0f, 0.0f, 0.0f };
};
}
