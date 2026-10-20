using namespace CoreEngine;

// 元のゲーム（TD3）の UIImage を、今の UI 画像の上で再現する入れ物。
// 画面は 1920×1080 の px で、左上が原点・下向きが +Y（元と同じ）。
// 色は線形の値で持ち、今の UI 画像へは sRGB に直して渡す。
// 描画順は元の renderOrder（SetSortOrder を呼べばその値、呼ばなければ 10800）

const int kTd3DefaultUIOrder = 10800;

class Td3UIImage
{
    UIImage@ image;
    private Vector2 textureSize_;
    private Vector2 anchoredPosition_ = Vector2(0.0f, 0.0f);
    private Vector2 size_ = Vector2(0.0f, 0.0f);
    private Vector4 color_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    private bool active_ = true;

    void Bind(UIImage@ target, const string &in texture)
    {
        @image = target;
        textureSize_ = Td3TextureSize(texture);
        // 元の UILayout の既定（中央のアンカー・中央の基準点・テクスチャの大きさ）
        image.anchor = UIAnchor::Center;
        image.pivot = Vector2(0.5f, 0.5f);
        image.anchoredPosition = anchoredPosition_;
        size_ = textureSize_;
        image.size = size_;
        image.rotation = 0.0f;
        image.sortOrder = kTd3DefaultUIOrder;
        image.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    Vector2 GetTextureSize() const { return textureSize_; }

    void SetAnchor(UIAnchor anchor) { image.anchor = anchor; }
    void SetPivot(const Vector2 &in pivot) { image.pivot = pivot; }
    void SetRotation(float rotation) { image.rotation = rotation; }
    void SetSortOrder(int order) { image.sortOrder = order; }

    void SetAnchoredPosition(const Vector2 &in position)
    {
        if (anchoredPosition_ == position) {
            return;
        }
        anchoredPosition_ = position;
        image.anchoredPosition = position;
    }

    void SetSize(const Vector2 &in size)
    {
        if (size_ == size) {
            return;
        }
        size_ = size;
        image.size = size;
    }

    void SetColor(const Vector4 &in color)
    {
        if (color_ == color) {
            return;
        }
        color_ = color;
        image.color = Td3LinearToSrgb(color);
    }

    // 縦に並んだコマの 1 つを切り出す（元の UV 変換の縦スライス）
    void SetVerticalFrame(int rows, int frame)
    {
        image.columns = 1;
        image.rows = rows;
        image.frame = frame;
    }

    void SetActive(bool active)
    {
        if (active_ == active) {
            return;
        }
        active_ = active;
        image.gameObject.active = active;
    }

    bool IsActive() const { return active_; }

    void SetMaterial(const string &in path) { image.material = path; }
    void SetMaterialFloat(const string &in name, float value) { image.SetMaterialFloat(name, value); }
    void SetMaterialVector(const string &in name, const Vector4 &in value) { image.SetMaterialVector(name, value); }
}

// 元の CreateObject<UIImage>() ＋ Initialize(tex, name)
Td3UIImage@ CreateTd3UIImage(GameObject@ owner, const string &in texture, const string &in name)
{
    UIImage@ target = owner.SpawnUIImage(kTexturesDir + texture, name);
    if (target is null) {
        return null;
    }
    Td3UIImage image;
    image.Bind(target, kTexturesDir + texture);
    return image;
}
