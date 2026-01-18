#pragma once

#include <cmath>
#include <algorithm>
#include <numbers>

namespace CoreEngine
{
// 前方宣言（依存を最小限にするため）
struct Vector3;

/// @brief イージング関数ユーティリティ
namespace EasingUtil {

    /// @brief イージングタイプ
    /// 各イージングの動きの特徴をコメントで説明
    enum class Type {
    Linear, // 等速：開始も終了も速度が一定で直線的に変化

    // ===== Quadratic (2乗) - 基本的な加速・減速 =====

    EaseInQuad, // 緩やかに始まり徐々に加速する
    EaseOutQuad, // 速く始まり徐々に減速する
    EaseInOutQuad, // 始めと終わりが緩やかで中央で加速する（S字状）

    // ===== Cubic (3乗) - より強い加速・減速 =====

    EaseInCubic, // 非常に緩やかに始まり中盤で強く加速する
    EaseOutCubic, // 先に速く進み終盤で強く減速する
    EaseInOutCubic, // 両端が緩やかで中央がより強く変化する（はっきりしたS字）

    // ===== Quartic (4乗) - さらに強い加速・減速 =====

    EaseInQuart, // 極めて緩やかに始まり後半で急激に加速する
    EaseOutQuart, // 先に急速に進み終盤で極めて緩やかに減速する
    EaseInOutQuart, // 両端が非常に緩やかで中央で急激に変化する

    // ===== Quintic (5乗) - 最も強い加速・減速 =====

    EaseInQuint, // ごくゆっくり始まり終盤で急激に加速する
    EaseOutQuint, // 先に非常に急速に進み終盤でごく緩やかに止まる
    EaseInOutQuint, // 非常に強いS字で両端が緩やか、中央で最大の変化

    // ===== Sine - 滑らかな曲線（自然な動き） =====

    EaseInSine, // 正弦状で滑らかに始まり穏やかに加速する
    EaseOutSine, // 正弦状で滑らかに始まり終盤で穏やかに減速する
    EaseInOutSine, // 正弦状のS字で自然で滑らかな変化

    // ===== Exponential - 急激な変化（デジタル的） =====

    EaseInExpo, // 非常に急激に加速し始める（ほとんどゼロから急上昇）
    EaseOutExpo, // 終盤で急速に減速してほぼ瞬時に止まる
    EaseInOutExpo, // 両端が緩やかで中央が指数的に急変する

    // ===== Circular - 円弧状の変化（幾何学的） =====

    EaseInCirc, // 円弧的に緩やかに始まり中盤で加速する（滑らかな曲線）
    EaseOutCirc, // 先に速く進み終盤は円弧的に緩やかに止まる
    EaseInOutCirc, // 両端が緩やかで中央が円弧的に強く変化する

    // ===== Back - オーバーシュート（行き過ぎて戻る） =====

    EaseInBack, // 少し逆方向に引いてから目標へ向かい加速する（予備動作）
    EaseOutBack, // 目標を少し超えてから戻る（弾力のある着地）
    EaseInOutBack, // 両端で軽くオーバーシュートするダイナミックなS字

    // ===== Elastic - 弾性（ゴムのような反発） =====

    EaseInElastic, // 振動を伴いながら加速していく（伸び縮みする挙動）
    EaseOutElastic, // 目標で数回振動して収束する（バネの反発）
    EaseInOutElastic, // 開始と終了で振動を伴う強い弾性挙動

    // ===== Bounce - 跳ね返り（ボールの弾み） =====

    EaseInBounce, // 跳ねながら加速していく（複数回のバウンド）
    EaseOutBounce, // 地面で何度か弾んで最終的に止まる
    EaseInOutBounce // 両端でバウンドする遊びのある動き

    };

    /// @brief イージング関数を適用
    /// @param t 進行状況（0.0～1.0）
    /// @param type イージングタイプ
    /// @return イージング適用済み値（0.0～1.0）
    float Apply(float t, Type type);

    /// @brief 2つの値の間を補間
    /// @param start 開始値
    /// @param end 終了値
    /// @param t 進行状況（0.0～1.0）
    /// @param type イージングタイプ
    /// @return 補間された値
    float Lerp(float start, float end, float t, Type type = Type::Linear);

    /// @brief Vector3の補間
    /// @param start 開始ベクトル
    /// @param end 終了ベクトル
    /// @param t 進行状況（0.0～1.0）
    /// @param type イージングタイプ
    /// @return 補間されたベクトル
    Vector3 LerpVector3(const Vector3& start, const Vector3& end, float t, Type type = Type::Linear);

    /// @brief 角度の補間（最短経路、ラジアン）
    /// @param startAngle 開始角度
    /// @param endAngle 終了角度
    /// @param t 進行状況（0.0～1.0）
    /// @param type イージングタイプ
    /// @return 補間された角度
    float LerpAngle(float startAngle, float endAngle, float t, Type type = Type::Linear);

    /// @brief カラー値の補間（0.0～1.0）
    /// @param startR 開始赤成分
    /// @param startG 開始緑成分
    /// @param startB 開始青成分
    /// @param startA 開始アルファ成分
    /// @param endR 終了赤成分
    /// @param endG 終了緑成分
    /// @param endB 終了青成分
    /// @param endA 終了アルファ成分
    /// @param t 進行状況（0.0～1.0）
    /// @param type イージングタイプ
    /// @return 補間されたカラー値（RGBA）
    struct Color { float r, g, b, a; };
    Color LerpColor(float startR, float startG, float startB, float startA,
                   float endR, float endG, float endB, float endA,
                   float t, Type type = Type::Linear);

    
    /// @brief 複合イージング（2つの異なるイージングを組み合わせ）
    /// @param t 進行状況（0.0～1.0）
    /// @param firstType 前半のイージングタイプ
    /// @param secondType 後半のイージングタイプ
    /// @param switchPoint 切り替え点（0.0～1.0、デフォルト0.5）
    /// @return イージング適用済み値
    float ApplyComposite(float t, Type firstType, Type secondType, float switchPoint = 0.5f);

    /// @brief カスタムカーブイージング（ベジェ曲線ベース）
    /// @param t 進行状況（0.0～1.0）
    /// @param controlPoint1 制御点1（0.0～1.0）
    /// @param controlPoint2 制御点2（0.0～1.0）
    /// @return イージング適用済み値
    float ApplyCustomCurve(float t, float controlPoint1, float controlPoint2);

    /// @brief 振動を伴うイージング（カーブ弾の爽快感演出用）
    /// @param t 進行状況（0.0～1.0）
    /// @param baseType ベースとなるイージングタイプ
    /// @param oscillationFrequency 振動の周波数
    /// @param oscillationAmplitude 振動の振幅
    /// @return イージング適用済み値
    float ApplyOscillating(float t, Type baseType, float oscillationFrequency = 2.0f, float oscillationAmplitude = 0.1f);

    /// @brief イージングタイプ名を文字列で取得（デバッグ用）
    /// @param type イージングタイプ
    /// @return タイプ名文字列
    const char* GetTypeName(Type type);

    /// @brief イージングタイプの説明を取得（UI表示用）
    /// @param type イージングタイプ
    /// @return タイプの説明文字列
    const char* GetTypeDescription(Type type);

}
}
