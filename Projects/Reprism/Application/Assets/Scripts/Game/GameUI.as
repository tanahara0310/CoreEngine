using namespace CoreEngine;

// ゲーム画面の UI（元の GameButtonUI と入力の説明・UndoNotificationEffect・StageNumberUI）

const string kStageNumberMaterial = "Application/Assets/Materials/StageNumber.mat";
const string kUndoDustMaterial = "Application/Assets/Materials/UndoDust.mat";
const int kGameWordSortOrder = 50;

// ボタンの絵（UI_Button.png の縦 14 コマ）と説明の文字（UI_word.png の縦 6 コマ）、Undo の文字
class GameButtonUI
{
    private array<Td3UIImage@> images_;

    void Initialize(GameObject@ owner)
    {
        // ボタン：左上に K（切り替え）と Space（ジャンプ）、右上に Z（Undo）
        AddButton(owner, 0, UIAnchor::TopRight, Vector2(-430.0f, 29.36f), Vector2(160.94f, 73.34f));
        AddButton(owner, 10, UIAnchor::TopLeft, Vector2(-18.78f, 106.05f), Vector2(167.04f, 78.84f));
        AddButton(owner, 11, UIAnchor::TopLeft, Vector2(-11.75f, 28.78f), Vector2(155.33f, 76.50f));

        // 説明の文字
        AddWord(owner, 0, UIAnchor::TopLeft, Vector2(76.95f, 117.78f), Vector2(260.43f, 59.14f));
        AddWord(owner, 4, UIAnchor::TopLeft, Vector2(74.28f, 40.06f), Vector2(259.92f, 60.88f));

        Td3UIImage@ undoWord = CreateTd3UIImage(owner, "UI/UI_Undo.png", "GameUIUndoWord");
        undoWord.SetAnchor(UIAnchor::TopRight);
        undoWord.SetPivot(Vector2(0.0f, 0.0f));
        undoWord.SetAnchoredPosition(Vector2(-260.0f, 40.06f));
        undoWord.SetSize(Vector2(259.92f, 60.88f));
        undoWord.SetSortOrder(kGameWordSortOrder);
        undoWord.SetActive(false);
        images_.insertLast(undoWord);
    }

    private void AddButton(GameObject@ owner, int frame, UIAnchor anchor, const Vector2 &in position, const Vector2 &in size)
    {
        Td3UIImage@ image = CreateTd3UIImage(owner, "UI/UI_Button.png", "UI_Button_" + frame);
        image.SetVerticalFrame(14, frame);
        image.SetSortOrder(kGameWordSortOrder);
        image.SetAnchor(anchor);
        image.SetPivot(Vector2(0.0f, 0.0f));
        image.SetAnchoredPosition(position);
        image.SetSize(size);
        image.SetActive(false);
        images_.insertLast(image);
    }

    private void AddWord(GameObject@ owner, int frame, UIAnchor anchor, const Vector2 &in position, const Vector2 &in size)
    {
        Td3UIImage@ image = CreateTd3UIImage(owner, "UI/UI_word.png", "GameUIWord" + frame);
        image.SetVerticalFrame(6, frame);
        image.SetSortOrder(kGameWordSortOrder);
        image.SetAnchor(anchor);
        image.SetPivot(Vector2(0.0f, 0.0f));
        image.SetAnchoredPosition(position);
        image.SetSize(size);
        image.SetActive(false);
        images_.insertLast(image);
    }

    void SetVisible(bool visible)
    {
        for (uint i = 0; i < images_.length(); ++i) {
            images_[i].SetActive(visible);
        }
    }
}

// Undo したとき、画面の中央から浮き上がって崩れる「UNDO」と、画面全体の暗転
class UndoNotificationEntry
{
    Td3UIImage@ image;
    float timer = 0.0f;
    bool active = false;
}

class UndoNotificationEffect
{
    private array<UndoNotificationEntry@> pool_;
    private Vector2 imageSize_;
    private float flashTimer_ = -1.0f;

    void Initialize(GameObject@ owner)
    {
        for (int i = 0; i < 8; ++i) {
            UndoNotificationEntry entry;
            @entry.image = CreateTd3UIImage(owner, "UI/UI_Undo.png", "UndoNotification");
            entry.image.SetAnchor(UIAnchor::Center);
            entry.image.SetPivot(Vector2(0.5f, 0.5f));
            entry.image.SetAnchoredPosition(Vector2(0.0f, 0.0f));
            entry.image.SetMaterial(kUndoDustMaterial);
            entry.image.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, 0.0f));
            pool_.insertLast(entry);
        }
        // 絵の 2 倍の大きさで出す
        const Vector2 baseSize = pool_[0].image.GetTextureSize();
        imageSize_ = Vector2(baseSize.x * 2.0f, baseSize.y * 2.0f);
        for (uint i = 0; i < pool_.length(); ++i) {
            pool_[i].image.SetSize(imageSize_);
        }
    }

    void Play()
    {
        for (uint i = 0; i < pool_.length(); ++i) {
            UndoNotificationEntry@ e = pool_[i];
            if (e.active) {
                continue;
            }
            e.active = true;
            e.timer = 0.0f;
            e.image.SetAnchoredPosition(Vector2(0.0f, 0.0f));
            e.image.SetSize(imageSize_);
            e.image.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            e.image.SetMaterialFloat("time", 0.0f);
            e.image.SetMaterialFloat("dissolve", 0.0f);
            break;
        }

        // 画面全体を一瞬暗くする（フェードの黒）
        flashTimer_ = 0.0f;
        ShaderMaterial@ fade = PostEffect::GetMaterial("Fade");
        if (fade !is null) {
            fade.SetFloat("fadeType", 0.0f);
            SetFlashAlpha(fade, 0.8f);
            PostEffect::SetEnabled("Fade", true);
        }
    }

    private void SetFlashAlpha(ShaderMaterial@ fade, float alpha)
    {
        fade.SetFloat("progress", alpha / fade.GetFloat("completeFactor"));
    }

    void Update(float dt)
    {
        if (flashTimer_ >= 0.0f) {
            flashTimer_ += dt;
            ShaderMaterial@ fade = PostEffect::GetMaterial("Fade");
            float flashAlpha = 0.8f;
            if (flashTimer_ >= 0.12f) {
                const float t = Clamp((flashTimer_ - 0.12f) / 0.15f, 0.0f, 1.0f);
                flashAlpha = 0.8f * (1.0f - t);
            }
            if (fade !is null) {
                SetFlashAlpha(fade, flashAlpha);
            }
            if (flashTimer_ >= 0.12f + 0.15f) {
                flashTimer_ = -1.0f;
                if (fade !is null) {
                    fade.SetFloat("progress", 0.0f);
                }
                PostEffect::SetEnabled("Fade", false);
            }
        }

        for (uint i = 0; i < pool_.length(); ++i) {
            UndoNotificationEntry@ e = pool_[i];
            if (!e.active) {
                continue;
            }
            e.timer += dt;
            if (e.timer >= 0.6f) {
                e.image.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, 0.0f));
                e.image.SetSize(imageSize_);
                e.image.SetAnchoredPosition(Vector2(0.0f, 0.0f));
                e.active = false;
                e.timer = 0.0f;
                continue;
            }
            const float t = Clamp(e.timer / 0.6f, 0.0f, 1.0f);
            // 最初に速く上がって緩やかに止まる
            const float posY = -120.0f * sqrt(t);
            // 長く残って最後に急に消える
            const float alpha = 1.0f - t * t * t;
            // 後半から崩れ始める
            const float dissolveT = (t > 0.3f) ? (t - 0.3f) / 0.7f : 0.0f;
            e.image.SetAnchoredPosition(Vector2(0.0f, posY));
            e.image.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, alpha));
            e.image.SetMaterialFloat("time", e.timer);
            e.image.SetMaterialFloat("dissolve", dissolveT * dissolveT);
        }
    }
}

// ステージの始めに出る「STAGE n」と難しさの星。遷移が終わってから現れ、1.5 秒後にブラウン管のように消える
class StageNumberUI
{
    private Td3UIImage@ band_;
    private Td3UIImage@ stageLabel_;
    private array<Td3UIImage@> stars_;
    private Td3UIImage@ image_;
    private array<Td3UIImage@> all_;
    private int activeStarCount_ = 1;
    private float elapsedTime_ = 0.0f;
    private bool waiting_ = true;
    private bool appearingIn_ = false;
    private float appearTimer_ = 0.0f;
    private bool puchunStarted_ = false;
    private float puchunTimer_ = 0.0f;
    private bool finished_ = false;

    bool IsFinished() const { return finished_; }

    private int GetDifficultyStarCount(int stageNumber) const
    {
        switch (stageNumber) {
        case 1:
        case 2:
            return 1;
        case 3:
        case 4:
        case 6:
        case 7:
            return 2;
        case 5:
        case 8:
        case 9:
        case 11:
            return 3;
        case 10:
            return 4;
        case 12:
        case 13:
            return 5;
        }
        return 1;
    }

    private Vector4 StarColor(bool isActive, float alpha) const
    {
        return isActive ? Vector4(1.0f, 0.92f, 0.2f, alpha) : Vector4(1.0f, 1.0f, 1.0f, alpha);
    }

    private Td3UIImage@ Create(GameObject@ owner, const string &in texture, const string &in name, const Vector2 &in size,
        const Vector2 &in position, int sortOrder, const Vector4 &in materialColor)
    {
        Td3UIImage@ image = CreateTd3UIImage(owner, texture, name);
        image.SetSize(size);
        image.SetAnchor(UIAnchor::Center);
        image.SetPivot(Vector2(0.5f, 0.5f));
        image.SetAnchoredPosition(position);
        image.SetSortOrder(sortOrder);
        image.SetActive(false);
        image.SetMaterial(kStageNumberMaterial);
        image.SetMaterialVector("color", materialColor);
        image.SetMaterialFloat("puchunProgress", 0.0f);
        all_.insertLast(image);
        return image;
    }

    void Initialize(GameObject@ owner, int stageNumber)
    {
        activeStarCount_ = GetDifficultyStarCount(stageNumber);

        // 中央の帯（画面の幅いっぱい）
        @band_ = Create(owner, "white1x1.png", "StageNumberBand", Vector2(kScreenWidth, 280.0f), Vector2(0.0f, 0.0f), 10,
            Vector4(0.02f, 0.02f, 0.08f, 0.0f));

        // 「STAGE」の文字と番号を並べた組を、少し右へずらして中央に置く
        const float labelOffsetX = -(24.0f / 2.0f + 416.0f / 2.0f) + 140.0f;
        const float numberOffsetX = (24.0f / 2.0f + 156.0f / 2.0f) + 140.0f;
        @stageLabel_ = Create(owner, "UI/UI_Stage.png", "StageLabel", Vector2(416.0f, 104.0f), Vector2(labelOffsetX, 0.0f), 11,
            Vector4(1.0f, 1.0f, 1.0f, 0.0f));

        // 難しさの星（帯の下に 5 つ）
        for (int i = 0; i < 5; ++i) {
            const Vector2 textureSize = Td3TextureSize(kTexturesDir + "UI/UI_Star.png");
            const float starWidth = textureSize.x * 0.25f;
            const float starHeight = textureSize.y * 0.25f;
            const float totalWidth = 90.0f * 4.0f;
            const float offsetX = -totalWidth * 0.5f + float(i) * 90.0f;
            const float offsetY = 280.0f * 0.5f + 50.0f + starHeight * 0.5f;
            const Vector4 color = StarColor(i < activeStarCount_, 0.0f);
            Td3UIImage@ star = Create(owner, "UI/UI_Star.png", "StageDifficultyStar" + (i + 1), Vector2(starWidth, starHeight),
                Vector2(offsetX, offsetY), 11, color);
            star.SetColor(color);
            stars_.insertLast(star);
        }

        // 番号（UI_Number.png の横 30 コマから切り出す）
        @image_ = Create(owner, "UI/UI_Number.png", "StageNumberUI", Vector2(156.0f, 256.0f), Vector2(numberOffsetX, 0.0f), 11,
            Vector4(1.0f, 1.0f, 1.0f, 0.0f));
        const float scaleU = 156.0f / (156.0f * 30.0f);
        image_.SetMaterialVector("uvScale", Vector4(scaleU, 1.0f, 0.0f, 0.0f));
        image_.SetMaterialVector("uvOffset", Vector4(float(stageNumber - 1) * scaleU, 0.0f, 0.0f, 0.0f));

        waiting_ = true;
    }

    private void SetAllFloat(const string &in name, float value)
    {
        for (uint i = 0; i < all_.length(); ++i) {
            all_[i].SetMaterialFloat(name, value);
        }
    }

    void Update(float dt)
    {
        if (finished_ || image_ is null) {
            return;
        }

        // 遷移が終わるまで待つ
        if (waiting_) {
            if (Scene::IsTransitioning()) {
                return;
            }
            for (uint i = 0; i < all_.length(); ++i) {
                all_[i].SetActive(true);
            }
            waiting_ = false;
            appearingIn_ = true;
            band_.SetMaterialVector("color", Vector4(0.02f, 0.02f, 0.08f, 0.75f));
            image_.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            stageLabel_.SetMaterialVector("color", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            for (uint i = 0; i < stars_.length(); ++i) {
                const Vector4 starColor = StarColor(int(i) < activeStarCount_, 1.0f);
                stars_[i].SetColor(starColor);
                stars_[i].SetMaterialVector("color", starColor);
            }
            return;
        }

        elapsedTime_ += dt;
        SetAllFloat("time", elapsedTime_);

        // 中央の線から広がって現れる
        if (appearingIn_) {
            appearTimer_ += dt;
            const float progress = Min(appearTimer_ / 0.55f, 1.0f);
            SetAllFloat("appearProgress", progress);
            if (progress >= 1.0f) {
                appearingIn_ = false;
                elapsedTime_ = 0.0f;
            }
            return;
        }

        // ブラウン管のように潰れて消える
        if (puchunStarted_) {
            puchunTimer_ += dt;
            const float progress = Min(puchunTimer_ / 0.12f, 1.0f);
            SetAllFloat("puchunProgress", progress);
            if (progress >= 1.0f) {
                for (uint i = 0; i < all_.length(); ++i) {
                    all_[i].SetActive(false);
                }
                finished_ = true;
            }
            return;
        }

        if (elapsedTime_ >= 1.5f) {
            puchunStarted_ = true;
            puchunTimer_ = 0.0f;
        }
    }
}
