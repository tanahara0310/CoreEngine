#pragma once

#include "MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <string>

// 定数バッファ用データ

namespace CoreEngine
{
struct ConstantBufferDataWorldTransform {
	Matrix4x4 matWorld; // ワールド変換行列
};

/// <summary>
/// ワールドトランスフォームクラス
/// 3Dオブジェクトの位置・回転・スケールを管理し、GPU用の行列を生成する
/// </summary>
class WorldTransform {
public:
	// === 回転モード ===
	enum class RotationMode {
		Euler,      // オイラー角による回転
		Quaternion  // クォータニオンによる回転
	};

	// === トランスフォームパラメータ（直接アクセス可能） ===
	Vector3 scale = { 1.0f, 1.0f, 1.0f };      // スケール
	Vector3 rotate = { 0.0f, 0.0f, 0.0f };     // 回転角（ラジアン）- オイラー角モード用
	Vector3 translate = { 0.0f, 0.0f, 0.0f };  // 位置
	Quaternion quaternionRotate = { 0.0f, 0.0f, 0.0f, 1.0f }; // クォータニオン回転 - クォータニオンモード用

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
	/// ImGuiでTransform情報を表示・編集（デバッグ用）
	/// </summary>
	/// <param name="label">ラベル名</param>
	/// <returns>変更があった場合true</returns>
	bool DrawImGui(const std::string& label);

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

	/// <summary>
	/// 回転モードを設定
	/// </summary>
	/// <param name="mode">回転モード（Euler or Quaternion）</param>
	void SetRotationMode(RotationMode mode) { rotationMode_ = mode; }

	/// <summary>
	/// 回転モードを取得
	/// </summary>
	RotationMode GetRotationMode() const { return rotationMode_; }

	/// <summary>
	/// オイラー角からクォータニオンに変換して設定
	/// </summary>
	void EulerToQuaternion();

	/// <summary>
	/// クォータニオンからオイラー角に変換して設定
	/// </summary>
	void QuaternionToEuler();

private:
	// 定数バッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	// マッピング済みポインタ
	ConstantBufferDataWorldTransform* mapped_ = nullptr;
	// 計算済みワールド行列
	Matrix4x4 matWorld_;
	// 親トランスフォーム（階層構造用）
	const WorldTransform* parent_ = nullptr;
	// 回転モード
	RotationMode rotationMode_ = RotationMode::Euler;
};
}
