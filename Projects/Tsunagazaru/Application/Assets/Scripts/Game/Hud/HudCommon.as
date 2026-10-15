// ポーズメニューを開いている間は true。HUD と演出はこの間止まる（元のゲームは停止ボタンと同じ止め方だった）
namespace GamePause
{
    bool paused = false;
}

// 元のゲームの UI の色（トーンマップの前に描いていた・昼の露出が掛かる値）を、
// 画面に出ていた sRGB の色へ写す。元のコードに残っていた実測の対応を、対数で補間する
float OriginalHudChannel(float linear)
{
    const array<float> inputs = { 0.0f, 0.012f, 0.04f, 0.10f, 0.20f, 0.44f, 1.0f, 2.0f };
    const array<float> outputs = { 0.0f, 67.0f, 141.0f, 199.0f, 227.0f, 245.0f, 253.0f, 255.0f };
    if (linear <= 0.0f) {
        return 0.0f;
    }
    if (linear >= inputs[inputs.length() - 1]) {
        return 1.0f;
    }
    for (uint i = 1; i < inputs.length(); ++i) {
        if (linear <= inputs[i]) {
            if (i == 1) {
                return outputs[1] * (linear / inputs[1]) / 255.0f;
            }
            const float t = (log(linear) - log(inputs[i - 1])) / (log(inputs[i]) - log(inputs[i - 1]));
            return Lerp(outputs[i - 1], outputs[i], t) / 255.0f;
        }
    }
    return 1.0f;
}

Vector4 OriginalHudColor(const Vector4 &in linear)
{
    return Vector4(OriginalHudChannel(linear.x), OriginalHudChannel(linear.y), OriginalHudChannel(linear.z), linear.w);
}

// 元のゲームの昼の GameScene で、露出を打ち消さない HUD に掛かっていた露出（倍）
const float kOriginalHudExposure = 4.5f;

// トーンマップ（ACES の近似式）。0〜1 の線形の値を返す
float AcesFilm(float linear)
{
    const float x = Max(0.0f, linear);
    return Clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
}

float SrgbToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f : pow((Max(value, 0.0f) + 0.055f) / 1.055f, 2.4f);
}

// 1 を超える値もそのまま返す（UI の指定色は 1 を超えて版下を明るくできる）
float LinearToSrgb(float value)
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
}

// 元のゲームで露出を打ち消して描いていた UI の値を、トーンマップ（ACES）と sRGB を通した画面の色へ写す
float ToneMappedChannel(float linear)
{
    return LinearToSrgb(AcesFilm(linear));
}

// 元のゲームは UI をトーンマップの前に「指定色 × 版下（線形）× 露出 → ACES → sRGB」で描いていた。
// 今の UI は「指定色（sRGB）× 版下」なので、版下の平均の画素値（0〜255）で元の明るさに合う指定色を出す。
// gain は指定色と露出を掛けた値
float OriginalTextureTintChannel(float gain, float meanTexel)
{
    const float decoded = SrgbToLinear(Clamp(meanTexel / 255.0f, 0.0f, 1.0f));
    if (decoded < 1.0e-5f) {
        // 暗部の傾き（0.03 / 0.14）で倍率を決める
        return LinearToSrgb(Max(gain, 0.0f) * (0.03f / 0.14f));
    }
    return LinearToSrgb(AcesFilm(gain * decoded) / decoded);
}

Vector4 OriginalTextureTint(const Vector3 &in meanTexel, const Vector4 &in gain)
{
    return Vector4(OriginalTextureTintChannel(gain.x, meanTexel.x), OriginalTextureTintChannel(gain.y, meanTexel.y),
        OriginalTextureTintChannel(gain.z, meanTexel.z), gain.w);
}

// 版下の平均の画素値（不透明な所・0〜255）
const Vector3 kTexMeanPlank = Vector3(30.0f, 14.0f, 5.0f);
const Vector3 kTexMeanBoard = Vector3(12.0f, 6.0f, 2.0f);
const Vector3 kTexMeanBoardCap = Vector3(31.0f, 18.0f, 5.0f);
const Vector3 kTexMeanPlant = Vector3(10.0f, 27.0f, 6.0f);
const Vector3 kTexMeanCursor = Vector3(121.0f, 55.0f, 6.0f);
const Vector3 kTexMeanPip = Vector3(115.0f, 87.0f, 15.0f);
const Vector3 kTexMeanPipEmpty = Vector3(25.0f, 13.0f, 5.0f);
const Vector3 kTexMeanCart = Vector3(93.0f, 83.0f, 74.0f);
const Vector3 kTexMeanLoadingMonkey = Vector3(101.0f, 92.0f, 84.0f);
const Vector3 kTexMeanRail = Vector3(42.0f, 33.0f, 28.0f);

Vector4 ToneMappedColor(const Vector4 &in linear)
{
    return Vector4(ToneMappedChannel(linear.x), ToneMappedChannel(linear.y), ToneMappedChannel(linear.z), linear.w);
}

// シーンに保存しない UI 画像を作り、左上を基準に置く
UIImage@ SpawnHudImage(GameObject@ owner, const string &in texture, const string &in name, int sortOrder,
    const Vector2 &in pivot, const Vector2 &in size)
{
    UIImage@ image = owner.SpawnUIImage(texture, name);
    if (image is null) {
        return null;
    }
    image.anchor = UIAnchor::TopLeft;
    image.pivot = pivot;
    image.size = size;
    image.sortOrder = sortOrder;
    return image;
}

// シーンに保存しない UI 文字を作り、左上を基準に置く
UIText@ SpawnHudText(GameObject@ owner, const string &in font, const string &in text, const string &in name,
    int sortOrder, float fontSize)
{
    UIText@ label = owner.SpawnUIText(font, text, name);
    if (label is null) {
        return null;
    }
    label.anchor = UIAnchor::TopLeft;
    label.pivot = Vector2(0.5f, 0.5f);
    label.sortOrder = sortOrder;
    label.fontSize = fontSize;
    label.SetAlign(TextAlignH::Center, TextAlignV::Middle);
    return label;
}
