using namespace CoreEngine;

// 元のゲーム（TD3）の数学・補間・色の道具。元の式をそのまま写している

const float kPi = 3.14159265358979f;

// 元の EasingUtil::Type のうち、このゲームで使う種類
enum Td3Ease
{
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutQuart,
    EaseInQuint,
    EaseOutQuint,
    EaseInSine,
    EaseInOutSine,
    EaseInExpo,
    EaseOutBack
}

// 元の EasingUtil::Apply（t は 0〜1 に収めてから曲げる）
float Td3EaseApply(float t, Td3Ease type)
{
    t = Clamp(t, 0.0f, 1.0f);
    switch (type) {
    case Td3Ease::EaseInQuad:
        return t * t;
    case Td3Ease::EaseOutQuad:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Td3Ease::EaseInOutQuad:
        if (t < 0.5f) {
            return 2.0f * t * t;
        }
        return 1.0f - pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case Td3Ease::EaseInCubic:
        return t * t * t;
    case Td3Ease::EaseOutCubic:
        return 1.0f - pow(1.0f - t, 3.0f);
    case Td3Ease::EaseInOutCubic:
        if (t < 0.5f) {
            return 4.0f * t * t * t;
        }
        return 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Td3Ease::EaseOutQuart:
        return 1.0f - pow(1.0f - t, 4.0f);
    case Td3Ease::EaseInQuint:
        return t * t * t * t * t;
    case Td3Ease::EaseOutQuint:
        return 1.0f - pow(1.0f - t, 5.0f);
    case Td3Ease::EaseInSine:
        return 1.0f - cos((t * kPi) / 2.0f);
    case Td3Ease::EaseInOutSine:
        return -(cos(kPi * t) - 1.0f) / 2.0f;
    case Td3Ease::EaseInExpo:
        return t == 0.0f ? 0.0f : pow(2.0f, 10.0f * (t - 1.0f));
    case Td3Ease::EaseOutBack:
    {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * pow(t - 1.0f, 3.0f) + c1 * pow(t - 1.0f, 2.0f);
    }
    default:
        break;
    }
    return t;
}

// 元の EasingUtil::Lerp
float Td3EaseLerp(float start, float end, float t, Td3Ease type)
{
    return start + (end - start) * Td3EaseApply(t, type);
}

// 元の rand() % 2001 / 1000 - 1（-1〜1 を 0.001 刻み）
float Td3RandSigned()
{
    return float(Random::Range(0, 2000)) / 1000.0f - 1.0f;
}

int Td3Abs(int value)
{
    return value < 0 ? -value : value;
}

// 線形の値 → sRGB（今のスプライトと UI の色は sRGB で受け取り、シェーダで線形へ戻す）
float Td3LinearToSrgbChannel(float value)
{
    if (value < 0.0031308f) {
        return value * 12.92f;
    }
    return 1.055f * pow(Max(value, 0.0f), 1.0f / 2.4f) - 0.055f;
}

Vector4 Td3LinearToSrgb(const Vector4 &in color)
{
    return Vector4(Td3LinearToSrgbChannel(color.x), Td3LinearToSrgbChannel(color.y),
        Td3LinearToSrgbChannel(color.z), color.w);
}

// 元の GameTimer（このゲームで使う機能だけ）
class Td3Timer
{
    float currentTime = 0.0f;
    float duration = 0.0f;
    bool active = false;
    bool loop = false;
    bool finished = false;
    bool loopedThisFrame = false;

    void Update(float deltaTime)
    {
        if (!active) {
            return;
        }
        loopedThisFrame = false;
        currentTime += deltaTime;
        if (currentTime >= duration) {
            finished = true;
            if (loop) {
                currentTime = 0.0f;
                finished = false;
                loopedThisFrame = true;
            } else {
                active = false;
            }
        }
    }

    void Start(float newDuration, bool newLoop = false)
    {
        duration = newDuration;
        loop = newLoop;
        currentTime = 0.0f;
        active = true;
        finished = false;
        loopedThisFrame = false;
    }

    void Stop()
    {
        active = false;
    }

    void Reset()
    {
        currentTime = 0.0f;
        active = false;
        finished = false;
        loopedThisFrame = false;
    }

    bool IsActive() const { return active; }
    bool IsFinished() const { return finished; }
    bool HasLooped() const { return loopedThisFrame; }
    float GetElapsedTime() const { return currentTime; }
    float GetDuration() const { return duration; }

    float GetProgress() const
    {
        if (duration <= 0.0f) {
            return 1.0f;
        }
        return Min(1.0f, currentTime / duration);
    }

    float GetEasedProgress(Td3Ease type) const
    {
        return Td3EaseApply(GetProgress(), type);
    }
}
