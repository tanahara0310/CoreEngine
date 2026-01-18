#pragma once

#include "ParticleModule.h"
#include "MathCore.h"
/// @brief パーティクルのサイズ制御モジュール
/// 注意: 初期サイズの設定はMainModuleで行います
/// このモジュールはサイズの変化（Over Lifetime）のみを担当します

namespace CoreEngine
{
class SizeModule : public ParticleModule {
public:
    struct SizeData {
   float endSize = 0.0f;  // 終了サイズ（ライフタイムで線形補間）
        bool sizeOverLifetime = true;      // ライフタイムでサイズ変化させるか
        Vector3 endSize3D = {0.0f, 0.0f, 0.0f};   // 3D終了サイズ
        bool use3DSize = false;    // 3Dサイズを使用するか
      
        // サイズカーブ（線形補間のバリエーション）
        enum class SizeCurve {
  Linear,      // 線形変化
  EaseIn,      // 加速（最初ゆっくり、後半速く）
        EaseOut,     // 減速（最初速く、後半ゆっくり）
            EaseInOut,   // S字カーブ（最初と最後がゆっくり）
          Constant     // 一定（サイズ変化なし）
  };
      SizeCurve sizeCurve = SizeCurve::Linear;
        
        // 追加パラメータ
        float minSize = 0.01f;          // 最小サイズ制限
        float maxSize = 10.0f; // 最大サイズ制限
        bool uniformScaling = true;    // 均等スケーリング（3D使用時）
    };

    SizeModule();
    ~SizeModule() = default;

    /// @brief サイズデータを設定
/// @param data サイズデータ
    void SetSizeData(const SizeData& data) { sizeData_ = data; }

    /// @brief サイズデータを取得
    /// @return サイズデータの参照
    const SizeData& GetSizeData() const { return sizeData_; }

    /// @brief パーティクルのサイズを更新
  /// @param particle 対象のパーティクル
    void UpdateSize(Particle& particle);

#ifdef _DEBUG
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
bool ShowImGui() override;
#endif

private:
    SizeData sizeData_;

    /// @brief ライフタイム係数を取得
    /// @param particle パーティクル
    /// @return ライフタイム係数（0.0f - 1.0f）
    float GetLifetimeRatio(const Particle& particle);

    /// @brief カーブに基づいて値を補間
    /// @param t 補間係数（0.0f - 1.0f）
    /// @param curve カーブタイプ
    /// @return 補間された値（0.0f - 1.0f）
    float ApplyCurve(float t, SizeData::SizeCurve curve);

    /// @brief 2つのベクトルを線形補間
    /// @param start 開始ベクトル
    /// @param end 終了ベクトル
    /// @param t 補間係数（0.0f - 1.0f）
    /// @return 補間されたベクトル
    Vector3 LerpVector3(const Vector3& start, const Vector3& end, float t);

    /// @brief サイズにランダム性を適用
    /// @param baseSize ベースサイズ
    /// @param randomness ランダム性（0.0f-1.0f）
    /// @return ランダム性が適用されたサイズ
    float ApplyRandomness(float baseSize, float randomness);
};
}
