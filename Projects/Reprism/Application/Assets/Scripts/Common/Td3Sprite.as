using namespace CoreEngine;

// 元のゲーム（TD3）の SpriteObject を、今のスプライトの上で再現する入れ物。
// 元は「板 = テクスチャ全体の大きさ × scale」、今は「板 = UV の範囲の大きさ × scale」なので倍率を換算する。
// 座標は元の px をそのままワールド単位にする（pixelsPerUnit 1）。
// 色は元と同じ線形の値で持ち、今のスプライトへは sRGB に直して渡す。
// 描画順は元の renderOrder（SetSortingLayer・SetOrderInLayer を呼べば layer × 1000 + order、呼ばなければ 10700）

const string kSpritePrefab = "Application/Assets/Prefabs/Sprite.prefab";
const string kTexturesDir = "Application/Assets/Textures/";

// 元の描画順の既定（スプライトのパス 700 ＋ 半透明 10000）
const int kTd3DefaultSpriteOrder = 10700;

// ブレンド（今の SpriteRenderer.blendMode の値）
const int kBlendNormal = 1;
const int kBlendAdd = 2;

// テクスチャの大きさ（px）。読み込みを待たずに元の計算へ使う
Vector2 Td3TextureSize(const string &in path)
{
    if (path.findFirst("white1x1.png") >= 0) return Vector2(1.0f, 1.0f);
    if (path.findFirst("normalBlock.png") >= 0) return Vector2(64.0f, 64.0f);
    if (path.findFirst("unBreakBlock.png") >= 0) return Vector2(64.0f, 64.0f);
    if (path.findFirst("PlayerLeg.png") >= 0) return Vector2(32.0f, 32.0f);
    if (path.findFirst("player.png") >= 0) return Vector2(192.0f, 64.0f);
    if (path.findFirst("Player2.png") >= 0) return Vector2(128.0f, 64.0f);
    if (path.findFirst("Arrow.png") >= 0) return Vector2(128.0f, 128.0f);
    if (path.findFirst("Stage/stage") >= 0) return Vector2(817.0f, 447.0f);
    if (path.findFirst("UI_Number.png") >= 0) return Vector2(4680.0f, 156.0f);
    if (path.findFirst("UI_Stage.png") >= 0) return Vector2(384.0f, 96.0f);
    if (path.findFirst("UI_Stuck.png") >= 0) return Vector2(388.0f, 100.0f);
    if (path.findFirst("UI_Undo.png") >= 0) return Vector2(384.0f, 96.0f);
    if (path.findFirst("UI_Button.png") >= 0) return Vector2(256.0f, 1792.0f);
    if (path.findFirst("UI_StageIcon.png") >= 0) return Vector2(1024.0f, 1024.0f);
    if (path.findFirst("UI_Star.png") >= 0) return Vector2(512.0f, 512.0f);
    if (path.findFirst("UI_TitleLogo.png") >= 0) return Vector2(768.0f, 192.0f);
    if (path.findFirst("UI_Clear.png") >= 0) return Vector2(512.0f, 288.0f);
    if (path.findFirst("UI_word.png") >= 0) return Vector2(384.0f, 576.0f);
    if (path.findFirst("blackBackGround.png") >= 0) return Vector2(8320.0f, 720.0f);
    Warn("テクスチャ " + path + " の大きさが表にありません");
    return Vector2(1.0f, 1.0f);
}

funcdef void Td3AnimFinished();

// スプライトの位置・回転・大きさ
class Td3SpriteTransform
{
    Vector2 translate = Vector2(0.0f, 0.0f);
    Vector3 rotate = Vector3(0.0f, 0.0f, 0.0f);
    Vector2 scale = Vector2(1.0f, 1.0f);
}

// 元の SpriteAnimationClip（CreateUniform で作った 1 行のコマ）
class Td3AnimClip
{
    string name;
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int frameCount = 1;
    float frameDuration = 1.0f / 60.0f;
    bool loop = true;
}

// 元の SpriteAnimator
class Td3SpriteAnimator
{
    private array<Td3AnimClip@> clips_;
    private Td3AnimClip@ current_;
    private string currentName_;
    private int currentFrame_ = 0;
    private float frameTimer_ = 0.0f;
    private bool playing_ = false;
    Td3AnimFinished@ onFinished;

    void AddUniformClip(const string &in name, float frameWidth, float frameHeight, int frameCount, float fps,
        bool loop, float startX = 0.0f, float startY = 0.0f)
    {
        Td3AnimClip clip;
        clip.name = name;
        clip.left = startX;
        clip.top = startY;
        clip.width = frameWidth;
        clip.height = frameHeight;
        clip.frameCount = frameCount;
        clip.frameDuration = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 60.0f);
        clip.loop = loop;
        clips_.insertLast(clip);
    }

    void Play(const string &in name)
    {
        for (uint i = 0; i < clips_.length(); ++i) {
            if (clips_[i].name == name) {
                currentName_ = name;
                @current_ = clips_[i];
                currentFrame_ = 0;
                frameTimer_ = 0.0f;
                playing_ = true;
                return;
            }
        }
    }

    string GetCurrentClipName() const { return currentName_; }
    bool IsPlaying() const { return playing_; }

    void Update(float deltaTime, Td3Sprite@ sprite)
    {
        if (!playing_ || current_ is null || sprite is null || current_.frameCount == 0) {
            return;
        }
        ApplyFrame(sprite);
        frameTimer_ += deltaTime;
        if (frameTimer_ >= current_.frameDuration) {
            frameTimer_ -= current_.frameDuration;
            ++currentFrame_;
            if (currentFrame_ >= current_.frameCount) {
                if (current_.loop) {
                    currentFrame_ = 0;
                } else {
                    currentFrame_ = current_.frameCount - 1;
                    playing_ = false;
                    if (onFinished !is null) {
                        onFinished();
                    }
                }
            }
            ApplyFrame(sprite);
        }
    }

    private void ApplyFrame(Td3Sprite@ sprite)
    {
        if (current_ is null || currentFrame_ < 0 || currentFrame_ >= current_.frameCount) {
            return;
        }
        const Vector2 texSize = sprite.GetTextureSize();
        const float left = current_.left + float(currentFrame_) * current_.width;
        sprite.SetUVRect(left / texSize.x, current_.top / texSize.y,
            (left + current_.width) / texSize.x, (current_.top + current_.height) / texSize.y);
    }
}

class Td3Sprite
{
    GameObject@ object;
    private Transform@ transform_;
    private SpriteRenderer@ renderer_;
    private string texture_;
    private Vector2 textureSize_;

    private Vector2 translate_ = Vector2(0.0f, 0.0f);
    private Vector3 rotate_ = Vector3(0.0f, 0.0f, 0.0f);
    private Vector2 scale_ = Vector2(1.0f, 1.0f);
    private Vector2 uvMin_ = Vector2(0.0f, 0.0f);
    private Vector2 uvMax_ = Vector2(1.0f, 1.0f);
    private Vector2 anchor_ = Vector2(0.5f, 0.5f);
    private bool flipX_ = false;
    private Vector4 color_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    private int sortingLayer_ = 0;
    private int orderInLayer_ = 0;
    private bool orderSet_ = false;
    private int blendMode_ = kBlendNormal;
    private bool active_ = true;
    private string material_;

    Td3SpriteAnimator@ animator;

    void Bind(GameObject@ target, const string &in texture)
    {
        @object = target;
        @transform_ = target.transform;
        @renderer_ = target.spriteRenderer;
        SetTexture(texture);
        renderer_.anchor = anchor_;
        renderer_.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        renderer_.orderInLayer = kTd3DefaultSpriteOrder;
        renderer_.blendMode = blendMode_;
        ApplyTransform();
    }

    // ── テクスチャ ────────────────────────────────────────────────────────
    void SetTexture(const string &in texture)
    {
        texture_ = texture;
        textureSize_ = Td3TextureSize(texture);
        renderer_.texture = texture;
    }

    string GetTexture() const { return texture_; }
    Vector2 GetTextureSize() const { return textureSize_; }

    // テクスチャの大きさ × scale（元の GetActualSize）
    Vector2 GetActualSize() const
    {
        return Vector2(textureSize_.x * scale_.x, textureSize_.y * scale_.y);
    }

    // ── トランスフォーム ──────────────────────────────────────────────────
    Vector2 GetTranslate() const { return translate_; }
    Vector2 GetScale() const { return scale_; }
    Vector3 GetRotate() const { return rotate_; }

    void SetTranslate(float x, float y)
    {
        if (translate_.x == x && translate_.y == y) {
            return;
        }
        translate_ = Vector2(x, y);
        transform_.position = Vector3(x, y, 0.0f);
    }

    void SetTranslate(const Vector2 &in position)
    {
        SetTranslate(position.x, position.y);
    }

    void SetScale(float x, float y)
    {
        if (scale_.x == x && scale_.y == y) {
            return;
        }
        scale_ = Vector2(x, y);
        ApplyScale();
    }

    void SetRotate(const Vector3 &in rotate)
    {
        if (rotate_ == rotate) {
            return;
        }
        rotate_ = rotate;
        transform_.rotation = rotate;
    }

    void SetRotateZ(float z)
    {
        SetRotate(Vector3(rotate_.x, rotate_.y, z));
    }

    // 位置・回転・大きさをまとめて写し取る／戻す（元の EulerTransform の代入）
    Td3SpriteTransform@ GetTransform() const
    {
        Td3SpriteTransform transform;
        transform.translate = translate_;
        transform.rotate = rotate_;
        transform.scale = scale_;
        return transform;
    }

    void SetTransform(const Td3SpriteTransform@ transform)
    {
        SetTranslate(transform.translate);
        SetRotate(transform.rotate);
        SetScale(transform.scale.x, transform.scale.y);
    }

    // ── UV・アンカー・反転 ────────────────────────────────────────────────
    void SetUVRect(float left, float top, float right, float bottom)
    {
        if (uvMin_.x == left && uvMin_.y == top && uvMax_.x == right && uvMax_.y == bottom) {
            return;
        }
        uvMin_ = Vector2(left, top);
        uvMax_ = Vector2(right, bottom);
        renderer_.uvMin = uvMin_;
        renderer_.uvMax = uvMax_;
        ApplyScale();
    }

    // テクスチャの px で範囲を切り出す（元の SetTextureRect）
    void SetTextureRect(float left, float top, float width, float height)
    {
        SetUVRect(left / textureSize_.x, top / textureSize_.y,
            (left + width) / textureSize_.x, (top + height) / textureSize_.y);
    }

    void SetAnchor(const Vector2 &in anchor)
    {
        if (anchor_ == anchor) {
            return;
        }
        anchor_ = anchor;
        renderer_.anchor = anchor;
    }

    void SetFlipX(bool flip)
    {
        if (flipX_ == flip) {
            return;
        }
        flipX_ = flip;
        renderer_.flipX = flip;
    }

    // ── 色・ブレンド・描画順 ──────────────────────────────────────────────
    void SetColor(const Vector4 &in color)
    {
        if (color_ == color) {
            return;
        }
        color_ = color;
        renderer_.color = Td3LinearToSrgb(color);
    }

    Vector4 GetColor() const { return color_; }

    void SetBlendMode(int blend)
    {
        if (blendMode_ == blend) {
            return;
        }
        blendMode_ = blend;
        renderer_.blendMode = blend;
    }

    void SetSortingLayer(int layer)
    {
        sortingLayer_ = layer;
        orderSet_ = true;
        renderer_.orderInLayer = sortingLayer_ * 1000 + orderInLayer_;
    }

    void SetOrderInLayer(int order)
    {
        orderInLayer_ = order;
        orderSet_ = true;
        renderer_.orderInLayer = sortingLayer_ * 1000 + orderInLayer_;
    }

    // ── 表示 ──────────────────────────────────────────────────────────────
    void SetActive(bool active)
    {
        if (active_ == active) {
            return;
        }
        active_ = active;
        object.active = active;
    }

    bool IsActive() const { return active_; }

    // ── マテリアル ────────────────────────────────────────────────────────
    void SetMaterial(const string &in path)
    {
        if (material_ == path) {
            return;
        }
        material_ = path;
        renderer_.material = path;
    }

    string GetMaterial() const { return material_; }

    void SetMaterialFloat(const string &in name, float value)
    {
        renderer_.SetMaterialFloat(name, value);
    }

    void SetMaterialVector(const string &in name, const Vector4 &in value)
    {
        renderer_.SetMaterialVector(name, value);
    }

    void ClearMaterialOverrides()
    {
        renderer_.ClearMaterialOverrides();
    }

    // ── アニメーション ────────────────────────────────────────────────────
    Td3SpriteAnimator@ GetAnimator()
    {
        if (animator is null) {
            @animator = Td3SpriteAnimator();
        }
        return animator;
    }

    // 元の SpriteObject::Update（オブジェクト側の更新）。再生中のアニメを進める
    void ObjectUpdate(float deltaTime)
    {
        if (!active_) {
            return;
        }
        if (animator !is null && animator.IsPlaying()) {
            animator.Update(deltaTime, this);
        }
    }

    void Destroy()
    {
        if (object !is null) {
            object.Destroy();
        }
    }

    private void ApplyScale()
    {
        // 今の板は UV の範囲の大きさなので、元の「テクスチャ全体 × scale」になるよう割り戻す
        const float rangeX = abs(uvMax_.x - uvMin_.x);
        const float rangeY = abs(uvMax_.y - uvMin_.y);
        const float sx = rangeX > 0.0f ? scale_.x / rangeX : 0.0f;
        const float sy = rangeY > 0.0f ? scale_.y / rangeY : 0.0f;
        transform_.scale = Vector3(sx, sy, 1.0f);
    }

    private void ApplyTransform()
    {
        transform_.position = Vector3(translate_.x, translate_.y, 0.0f);
        transform_.rotation = rotate_;
        ApplyScale();
    }
}

// 元の CreateObject<SpriteObject>() ＋ Initialize(tex, name)
class Td3SpriteFactory
{
    GameObject@ owner;

    Td3SpriteFactory(GameObject@ target)
    {
        @owner = target;
    }

    Td3Sprite@ Create(const string &in texture, const string &in name)
    {
        GameObject@ object = owner.InstantiatePrefab(kSpritePrefab, name);
        if (object is null) {
            return null;
        }
        Td3Sprite sprite;
        sprite.Bind(object, kTexturesDir + texture);
        return sprite;
    }
}
