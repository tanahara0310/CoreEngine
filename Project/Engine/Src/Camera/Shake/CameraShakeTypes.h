#pragma once

#include "Math/Easing/EasingUtil.h"
#include "Math/Vector/Vector3.h"

#include <cmath>
#include <cstdint>

/// @file
/// @brief カメラシェイクのデータ型（揺れの定義と、その結果として出る姿勢の加算分）

namespace CoreEngine
{
    /// @brief 再生中の揺れ 1 件を識別するハンドル（0 = 無効）
    using ShakeHandle = std::uint32_t;

    /// @brief 揺れの波形
    /// @note いずれも「経過時間」から値を決める。毎フレーム乱数を引くと 60fps と 144fps で
    ///       別物の揺れになるため、フレームレート非依存にするにはこの形しかない。
    enum class ShakeWaveform {
        Perlin, ///< 連続で滑らかな揺れ。手ブレ・エンジン振動・継続的な揺れ（既定）
        Random, ///< frequency Hz で値を取り直して線形補間する粗い揺れ。被弾・破壊
        Sine,   ///< 規則的な振動。機械・共振・コミカルな演出
        Kick,   ///< 減衰振動。発射反動・着地など「一撃で蹴られる」揺れ
    };

    /// @brief 揺れを組み立てる座標系
    enum class ShakeSpace {
        CameraLocal, ///< カメラのローカル軸。カメラの向きに依らず画面上の見え方が一定（既定）
        World,       ///< ワールド軸。揺れの向きが世界に固定される（地震など）
    };

    /// @brief 揺れを進める時間
    /// @note Tween の TweenUpdate と同じ考え方。ヒットストップ演出は Unscaled が定番。
    enum class ShakeTimeMode {
        Scaled,   ///< Time::DeltaTime()。ヒットストップ・スローの影響を受ける
        Unscaled, ///< Time::UnscaledDeltaTime()。ポーズ中も進む
    };

    /// @brief 揺れ 1 件の定義
    /// @details 「どう揺れるか」だけを持つ POD。カメラにもエンジンにも依存しないので、
    ///          プリセット化・JSON 化してゲームごとに差し替えられる。
    ///          汎用性はこの構造体の表現力ではなく、プリセットの差し替えやすさで担保する。
    struct CameraShakeParams {
        // ===== 振幅 =====

        /// @brief 位置の振幅（メートル）
        /// @note 位置の揺れは壁や地面へめり込む。既定を 0 にして回転で作るのが安全で、
        ///       使う場合も cm オーダーに留めること。
        Vector3 positionAmplitude = { 0.0f, 0.0f, 0.0f };

        /// @brief 回転の振幅（度・pitch / yaw / roll）
        /// @note 回転はめり込まず、少ない振幅で強く読める。揺れの主役はこちら。
        Vector3 rotationAmplitude = { 0.5f, 0.5f, 1.0f };

        /// @brief 画角の振幅（度）。正射影カメラでは無視される
        float fovAmplitude = 0.0f;

        // ===== 周波数 =====

        /// @brief 基準周波数（Hz）
        float frequency = 18.0f;

        /// @brief 軸ごとの周波数倍率
        /// @note 3 軸を同じ周波数にすると揺れが 1 本の線上を往復して見える。既定のように少しずらす。
        Vector3 frequencyScale = { 1.0f, 1.13f, 0.87f };

        // ===== 時間 =====

        /// @brief 継続時間（秒）。0 以下で無限（Stop / StopAll まで続く）
        float duration = 0.35f;

        /// @brief 立ち上がりにかける時間（秒）。0 なら最初から最大振幅
        float attack = 0.0f;

        /// @brief 減衰カーブ（振幅が 1 → 0 になる形）
        EasingUtil::Type decayEase = EasingUtil::Type::EaseOutQuad;

        // ===== 方向性 =====

        /// @brief 揺れを寄せる向き（space で指定した座標系で解釈する。長さは正規化される）
        /// @note 爆発は「爆心から押される」、銃は「後方へ蹴られる」のように、実際の演出は
        ///       ほとんど向きを持つ。等方なノイズだけだと全部同じ揺れに見える。
        Vector3 direction = { 0.0f, 0.0f, 0.0f };

        /// @brief 方向性の強さ（0 = 等方なノイズ / 1 = 完全に方向づけられた揺れ）
        /// @note 位置は direction の向きへ寄る。回転は direction では表せないので、
        ///       代わりに「3 軸が同じ位相で揃って振れる」形になり、軸ごとの強さは
        ///       rotationAmplitude がそのまま決める。
        float directionality = 0.0f;

        // ===== 空間減衰 =====

        /// @brief 発生源からの距離で弱めるか（爆発・着弾など「発生源のある揺れ」）
        /// @note true のときは Play(params, worldOrigin) で発生源を渡すこと。
        bool useWorldFalloff = false;

        /// @brief この距離までは減衰しない
        float innerRadius = 0.0f;

        /// @brief この距離で振幅が 0 になる
        float outerRadius = 30.0f;

        /// @brief 減衰カーブ
        EasingUtil::Type falloffEase = EasingUtil::Type::EaseOutQuad;

        // ===== 種類 =====

        ShakeWaveform waveform = ShakeWaveform::Perlin;
        ShakeSpace    space    = ShakeSpace::CameraLocal;
        ShakeTimeMode timeMode = ShakeTimeMode::Scaled;

        /// @brief 乱数シード。0 なら再生ごとに自動採番する
        /// @note リプレイ・ネットワーク同期があるなら明示すること（波形は時間から決定的に決まる）。
        std::uint32_t seed = 0;
    };

    /// @brief 揺れが生む「基準姿勢への加算分」
    /// @details カメラの姿勢そのものではなく差分だけを持つ。基準姿勢を書き換えないための器。
    struct CameraShakeOffset {
        /// @brief カメラローカル空間の平行移動（+z = 視線方向）
        Vector3 localPosition = { 0.0f, 0.0f, 0.0f };

        /// @brief カメラローカル空間の回転（ラジアン・pitch / yaw / roll）
        /// @note 回転は常にカメラローカル。ワールド軸で首を振るとカメラの向きで揺れ方が変わる。
        Vector3 localRotation = { 0.0f, 0.0f, 0.0f };

        /// @brief ワールド空間の平行移動
        Vector3 worldPosition = { 0.0f, 0.0f, 0.0f };

        /// @brief 画角の加算分（度）
        float fovDegrees = 0.0f;

        /// @brief 反映する価値がないほど小さいか（呼び出し側の早期リターン用）
        bool IsNegligible() const
        {
            constexpr float kPositionEpsilon = 1.0e-6f; // メートル
            constexpr float kRotationEpsilon = 1.0e-6f; // ラジアン
            constexpr float kFovEpsilon = 1.0e-4f;      // 度

            return LengthSquared(localPosition) <= kPositionEpsilon * kPositionEpsilon
                && LengthSquared(worldPosition) <= kPositionEpsilon * kPositionEpsilon
                && LengthSquared(localRotation) <= kRotationEpsilon * kRotationEpsilon
                && std::abs(fovDegrees) <= kFovEpsilon;
        }
    };
}
