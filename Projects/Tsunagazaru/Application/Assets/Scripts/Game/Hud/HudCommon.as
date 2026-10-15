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
