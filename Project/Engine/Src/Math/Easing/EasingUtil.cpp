#include "EasingUtil.h"
#include "Math/MathCore.h"  // ★★★ MyMathの代わりにMathCoreを使用 ★★★
#include <numbers>


namespace CoreEngine
{
namespace EasingUtil {

float Apply(float t, Type type) {
    // tを0.0～1.0の範囲にクランプ
    t = std::clamp(t, 0.0f, 1.0f);
    
    switch (type) {
        case Type::Linear:
            return t;
            
        // ===== Quadratic =====
        case Type::EaseInQuad:
            return t * t;
            
        case Type::EaseOutQuad:
            return 1.0f - (1.0f - t) * (1.0f - t);
            
        case Type::EaseInOutQuad:
            if (t < 0.5f) {
                return 2.0f * t * t;
            } else {
                return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
            }
            
        // ===== Cubic =====
        case Type::EaseInCubic:
            return t * t * t;
            
        case Type::EaseOutCubic:
            return 1.0f - std::pow(1.0f - t, 3.0f);
            
        case Type::EaseInOutCubic:
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            } else {
                return 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
            }
            
        // ===== Quartic =====
        case Type::EaseInQuart:
            return t * t * t * t;
            
        case Type::EaseOutQuart:
            return 1.0f - std::pow(1.0f - t, 4.0f);
            
        case Type::EaseInOutQuart:
            if (t < 0.5f) {
                return 8.0f * t * t * t * t;
            } else {
                return 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
            }
            
        // ===== Quintic =====
        case Type::EaseInQuint:
            return t * t * t * t * t;
            
        case Type::EaseOutQuint:
            return 1.0f - std::pow(1.0f - t, 5.0f);
            
        case Type::EaseInOutQuint:
            if (t < 0.5f) {
                return 16.0f * t * t * t * t * t;
            } else {
                return 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
            }
            
        // ===== Sine =====
        case Type::EaseInSine:
            return 1.0f - std::cos((t * std::numbers::pi_v<float>) / 2.0f);
            
        case Type::EaseOutSine:
            return std::sin((t * std::numbers::pi_v<float>) / 2.0f);
            
        case Type::EaseInOutSine:
            return -(std::cos(std::numbers::pi_v<float> * t) - 1.0f) / 2.0f;
            
        // ===== Exponential =====
        case Type::EaseInExpo:
            return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
            
        case Type::EaseOutExpo:
            return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
            
        case Type::EaseInOutExpo:
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            if (t < 0.5f) {
                return std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f;
            } else {
                return (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
            }
            
        // ===== Circular =====
        case Type::EaseInCirc:
            return 1.0f - std::sqrt(1.0f - t * t);
            
        case Type::EaseOutCirc:
            return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f));
            
        case Type::EaseInOutCirc:
            if (t < 0.5f) {
                return (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f;
            } else {
                return (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
            }
            
        // ===== Back =====
        case Type::EaseInBack: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
        
        case Type::EaseOutBack: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        
        case Type::EaseInOutBack: {
            const float c1 = 1.70158f;
            const float c2 = c1 * 1.525f;
            if (t < 0.5f) {
                return (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f;
            } else {
                return (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
            }
        }
        
        // ===== Elastic =====
        case Type::EaseInElastic: {
            const float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        }
        
        case Type::EaseOutElastic: {
            const float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        
        case Type::EaseInOutElastic: {
            const float c5 = (2.0f * std::numbers::pi_v<float>) / 4.5f;
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            if (t < 0.5f) {
                return -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f;
            } else {
                return (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
            }
        }
        
        // ===== Bounce =====
        case Type::EaseOutBounce: {
            const float n1 = 7.5625f;
            const float d1 = 2.75f;
            
            if (t < 1.0f / d1) {
                return n1 * t * t;
            } else if (t < 2.0f / d1) {
                return n1 * (t -= 1.5f / d1) * t + 0.75f;
            } else if (t < 2.5f / d1) {
                return n1 * (t -= 2.25f / d1) * t + 0.9375f;
            } else {
                return n1 * (t -= 2.625f / d1) * t + 0.984375f;
            }
        }
        
        case Type::EaseInBounce:
            return 1.0f - Apply(1.0f - t, Type::EaseOutBounce);
            
        case Type::EaseInOutBounce:
            if (t < 0.5f) {
                return (1.0f - Apply(1.0f - 2.0f * t, Type::EaseOutBounce)) / 2.0f;
            } else {
                return (1.0f + Apply(2.0f * t - 1.0f, Type::EaseOutBounce)) / 2.0f;
            }
            
        default:
            return t;
    }
}

float Lerp(float start, float end, float t, Type type) {
    float easedT = Apply(t, type);
    return start + (end - start) * easedT;
}

Vector3 LerpVector3(const Vector3& start, const Vector3& end, float t, Type type) {
    float easedT = Apply(t, type);
    
    // ★★★ MathCoreを使用したベクトル補間 ★★★
    // start + (end - start) * easedT の計算をMathCore関数で実行
    Vector3 diff = MathCore::Vector::Subtract(end, start);
    Vector3 scaledDiff = MathCore::Vector::Multiply(easedT, diff);
    return MathCore::Vector::Add(start, scaledDiff);
}

float LerpAngle(float startAngle, float endAngle, float t, Type type) {
    float easedT = Apply(t, type);
    
    // 角度差を-π～πの範囲に正規化（最短経路）
    float diff = endAngle - startAngle;
    while (diff > std::numbers::pi_v<float>) {
        diff -= 2.0f * std::numbers::pi_v<float>;
    }
    while (diff < -std::numbers::pi_v<float>) {
        diff += 2.0f * std::numbers::pi_v<float>;
    }
    
    return startAngle + diff * easedT;
}

Color LerpColor(float startR, float startG, float startB, float startA,
               float endR, float endG, float endB, float endA,
               float t, Type type) {
    float easedT = Apply(t, type);
    
    return {
        startR + (endR - startR) * easedT,
        startG + (endG - startG) * easedT,
        startB + (endB - startB) * easedT,
        startA + (endA - startA) * easedT
    };
}

// ===== EasingCoreから移行した高度な機能 =====

float ApplyComposite(float t, Type firstType, Type secondType, float switchPoint) {
    switchPoint = (std::max)(0.0f, (std::min)(1.0f, switchPoint));
    
    if (t <= switchPoint) {
        // 前半
        float normalizedT = t / switchPoint;
        return Apply(normalizedT, firstType) * switchPoint;
    } else {
        // 後半
        float normalizedT = (t - switchPoint) / (1.0f - switchPoint);
        return switchPoint + Apply(normalizedT, secondType) * (1.0f - switchPoint);
    }
}

float ApplyCustomCurve(float t, float controlPoint1, float controlPoint2) {
    // 3次ベジェ曲線（P0=0, P1=controlPoint1, P2=controlPoint2, P3=1）
    float oneMinusT = 1.0f - t;
    float oneMinusT2 = oneMinusT * oneMinusT;
    float t2 = t * t;
    float t3 = t2 * t;
    
    return 3.0f * oneMinusT2 * t * controlPoint1 + 
           3.0f * oneMinusT * t2 * controlPoint2 + 
           t3;
}

float ApplyOscillating(float t, Type baseType, float oscillationFrequency, float oscillationAmplitude) {
    float baseValue = Apply(t, baseType);
    float oscillation = std::sin(t * oscillationFrequency * 2.0f * std::numbers::pi_v<float>) * oscillationAmplitude * (1.0f - t);
    return (std::max)(0.0f, (std::min)(1.0f, baseValue + oscillation));
}

const char* GetTypeName(Type type) {
    switch (type) {
        case Type::Linear: return "Linear";
        case Type::EaseInQuad: return "EaseInQuad";
        case Type::EaseOutQuad: return "EaseOutQuad";
        case Type::EaseInOutQuad: return "EaseInOutQuad";
        case Type::EaseInCubic: return "EaseInCubic";
        case Type::EaseOutCubic: return "EaseOutCubic";
        case Type::EaseInOutCubic: return "EaseInOutCubic";
        case Type::EaseInQuart: return "EaseInQuart";
        case Type::EaseOutQuart: return "EaseOutQuart";
        case Type::EaseInOutQuart: return "EaseInOutQuart";
        case Type::EaseInQuint: return "EaseInQuint";
        case Type::EaseOutQuint: return "EaseOutQuint";
        case Type::EaseInOutQuint: return "EaseInOutQuint";
        case Type::EaseInSine: return "EaseInSine";
        case Type::EaseOutSine: return "EaseOutSine";
        case Type::EaseInOutSine: return "EaseInOutSine";
        case Type::EaseInExpo: return "EaseInExpo";
        case Type::EaseOutExpo: return "EaseOutExpo";
        case Type::EaseInOutExpo: return "EaseInOutExpo";
        case Type::EaseInCirc: return "EaseInCirc";
        case Type::EaseOutCirc: return "EaseOutCirc";
        case Type::EaseInOutCirc: return "EaseInOutCirc";
        case Type::EaseInBack: return "EaseInBack";
        case Type::EaseOutBack: return "EaseOutBack";
        case Type::EaseInOutBack: return "EaseInOutBack";
        case Type::EaseInElastic: return "EaseInElastic";
        case Type::EaseOutElastic: return "EaseOutElastic";
        case Type::EaseInOutElastic: return "EaseInOutElastic";
        case Type::EaseInBounce: return "EaseInBounce";
        case Type::EaseOutBounce: return "EaseOutBounce";
        case Type::EaseInOutBounce: return "EaseInOutBounce";
        default: return "Unknown";
    }
}

const char* GetTypeDescription(Type type) {
    switch (type) {
        case Type::Linear: return "等速直線運動 - 一定速度で変化";
        case Type::EaseInQuad: return "ゆっくり始まって徐々に加速 - 滑らかな立ち上がり";
        case Type::EaseOutQuad: return "速く始まって徐々に減速 - 滑らかな着地";
        case Type::EaseInOutQuad: return "開始と終了で緩やか、中央で速い - S字カーブ";
        case Type::EaseInCubic: return "Quadより強い加速 - より急激な立ち上がり";
        case Type::EaseOutCubic: return "Quadより強い減速 - より急激な着地";
        case Type::EaseInOutCubic: return "Quadより強いS字カーブ - よりはっきりした変化";
        case Type::EaseInQuart: return "非常に緩やかに始まり急激に加速 - ドラマチックな立ち上がり";
        case Type::EaseOutQuart: return "急激に始まり非常に緩やかに減速 - ドラマチックな着地";
        case Type::EaseInOutQuart: return "両端で非常に緩やか、中央で急激 - 強いコントラスト";
        case Type::EaseInQuint: return "極めて緩やかに始まり極めて急激に加速 - 最もドラマチック";
        case Type::EaseOutQuint: return "極めて急激に始まり極めて緩やかに減速 - 最も柔らかい着地";
        case Type::EaseInOutQuint: return "最も強いS字カーブ - 極端なコントラスト";
        case Type::EaseInSine: return "正弦波の滑らかな加速 - 自然で心地よい立ち上がり";
        case Type::EaseOutSine: return "正弦波の滑らかな減速 - 自然で心地よい着地";
        case Type::EaseInOutSine: return "正弦波のS字カーブ - 最も自然な変化";
        case Type::EaseInExpo: return "指数関数的加速 - 突然の急激な変化";
        case Type::EaseOutExpo: return "指数関数的減速 - 急激に止まる";
        case Type::EaseInOutExpo: return "指数関数的S字 - デジタル的で機械的な変化";
        case Type::EaseInCirc: return "円弧の加速 - 幾何学的で美しい立ち上がり";
        case Type::EaseOutCirc: return "円弧の減速 - 幾何学的で美しい着地";
        case Type::EaseInOutCirc: return "円弧のS字 - 完璧な幾何学的バランス";
        case Type::EaseInBack: return "逆方向に少し動いてから目標方向へ - 予備動作のような効果";
        case Type::EaseOutBack: return "目標を超えて動いてから戻る - 弾力のある着地";
        case Type::EaseInOutBack: return "両端でオーバーシュート - ダイナミックで印象的";
        case Type::EaseInElastic: return "複数回振動しながら加速 - ゴムが伸びて縮む効果";
        case Type::EaseOutElastic: return "目標で複数回振動して収束 - バネのような弾力";
        case Type::EaseInOutElastic: return "開始と終了で振動 - 非常に弾力的でユニーク";
        case Type::EaseInBounce: return "複数回跳ねながら加速 - ボールが転がる効果";
        case Type::EaseOutBounce: return "地面で複数回バウンド - ボールが着地する効果";
        case Type::EaseInOutBounce: return "両端で跳ね返り - 非常にプレイフルな動き";
        default: return "不明なイージングタイプ";
    }
}

} // namespace EasingUtil
}
