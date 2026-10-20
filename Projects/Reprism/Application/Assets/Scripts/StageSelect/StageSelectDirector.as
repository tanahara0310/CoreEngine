using namespace CoreEngine;

const float kSelectMoveDuration = 0.4f;
const float kSelectArrowAnimationDuration = 1.0f;
const float kSelectStageAnimationDuration = 1.0f;
const float kSelectMidStageAnimationScale = 1.05f;
const float kSelectStageSpacing = 1280.0f;
const float kSelectArrowOffsetX = 520.0f;
const float kSelectArrowBaseY = -90.0f;
const float kSelectArrowSwingDistance = 16.0f;
const float kSelectStageNumberIconScale = 0.125f;
const float kSelectBorderPadding = 18.0f;
const float kSelectBorderPaddingX = 35.0f;
const float kSelectStageScale = 0.7f;
const float kSelectNumberOffsetY = -240.0f;
const float kSelectNumberAboveMargin = 20.0f;
const float kSelectStickNavigateThreshold = 0.5f;
const float kSelectStickReleaseThreshold = 0.25f;

// ステージ選択（元の StageSelectScene）。左右でステージを選び、Space で遊ぶ・Esc でタイトルへ
[DisplayName("Reprism：ステージ選択")]
class StageSelectDirector : ScriptComponent
{
    private Td3SpriteFactory@ factory_;
    private array<Td3Sprite@> stageSprites_;
    private array<Td3Sprite@> stageNumberIconSprites_;
    private array<Td3Sprite@> stageNumberSprites_;
    private Td3Sprite@ leftArrow_;
    private Td3Sprite@ rightArrow_;
    private array<float> stagePositionsX_;
    private GameBackgroundEffect backgroundEffect_;
    private CyberGridBackground grid_;

    private Td3Sprite@ stageBorderSprite_;
    private ShaderMaterial@ stageBorderMaterial_;
    private float borderLoopTimer_ = 0.0f;
    private float borderLoopInterval_ = 1.2f;
    private float borderExpandDuration_ = 0.7f;

    private float moveTimer_ = 0.0f;
    private bool isMoving_ = false;
    private float arrowAnimationTimer_ = 0.0f;
    private float stageAnimationTimer_ = 0.0f;
    private float currentSpriteOffsetX_ = 0.0f;
    private int gamepadMoveDirectionLatch_ = 0;

    private float startSpriteOffset_ = 0.0f;
    private float endSpriteOffset_ = 0.0f;
    private float endLeftArrowAngle_ = 0.0f;
    private float endRightArrowAngle_ = 0.0f;

    private Sound@ bgm_;
    private GameObject@ cameraObject_;


    void Start()
    {
        @factory_ = Td3SpriteFactory(owner);

        for (int i = 0; i < kMaxStage; i++) {
            stagePositionsX_.insertLast(float(i) * kSelectStageSpacing);
        }
        currentSpriteOffsetX_ = stagePositionsX_[GetStageNumber() - 1];

        for (int i = 0; i < kMaxStage; i++) {
            const float relX = stagePositionsX_[i] - currentSpriteOffsetX_;
            Td3Sprite@ stage = factory_.Create("Stage/stage" + (i + 1) + ".png", "stage" + (i + 1));
            stage.SetColor(Vector4(0.4f, 0.4f, 0.4f, 1.0f));
            stage.SetScale(0.0f, 0.0f);
            stage.SetTranslate(relX, -70.0f);
            stageSprites_.insertLast(stage);

            // 番号の後ろの飾りと、UI_Number.png（30 コマ）から切り出した番号
            Td3Sprite@ icon = factory_.Create("UI/UI_StageIcon.png", "stageNumberIcon" + (i + 1));
            icon.SetColor(Vector4(1.0f, 0.0f, 1.0f, 1.0f));
            icon.SetTranslate(relX, kSelectNumberOffsetY);
            icon.SetScale(kSelectStageNumberIconScale, kSelectStageNumberIconScale);
            stageNumberIconSprites_.insertLast(icon);

            Td3Sprite@ number = factory_.Create("UI/UI_Number.png", "stageNumber" + (i + 1));
            const float kFrameCount = 30.0f;
            number.SetUVRect(float(i) / kFrameCount, 0.0f, float(i + 1) / kFrameCount, 1.0f);
            number.SetTranslate(relX, kSelectNumberOffsetY);
            number.SetScale(1.0f / kFrameCount, 1.0f);
            stageNumberSprites_.insertLast(number);
        }

        Td3Sprite@ black = factory_.Create("blackBackGround.png", "blackBackGround");
        black.SetSortingLayer(-10);

        @leftArrow_ = factory_.Create("Select/leftArrow.png", "leftArrow");
        leftArrow_.SetTranslate(-kSelectArrowOffsetX, kSelectArrowBaseY);
        @rightArrow_ = factory_.Create("Select/rightArrow.png", "rightArrow");
        rightArrow_.SetTranslate(kSelectArrowOffsetX, kSelectArrowBaseY);

        @cameraObject_ = owner.FindObject("MainCamera");
        if (cameraObject_ !is null) {
            cameraObject_.transform.position = Vector3(0.0f, 0.0f, -10.0f);
            cameraObject_.camera.orthographicSize = kScreenHeight * 0.5f / 1.5f;
        }

        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        @bgm_ = Audio::PlayScoped("Application/Assets/Audio/Bgm/StageSelect.mp3", params);

        backgroundEffect_.Initialize(factory_, 0, 0, 36);

        // 選んでいるステージの枠
        @stageBorderSprite_ = factory_.Create("white1x1.png", "stageBorder");
        stageBorderSprite_.SetBlendMode(kBlendNormal);
        stageBorderSprite_.SetSortingLayer(5);
        stageBorderSprite_.SetMaterial("Application/Assets/Materials/StageBorder.mat");
        @stageBorderMaterial_ = ShaderMaterial("Application/Assets/Materials/StageBorder.mat");
        stageBorderMaterial_.SetVector("color", Vector4(0.0f, 1.0f, 1.0f, 1.0f));
        stageBorderMaterial_.SetFloat("borderWidth", 0.015f);
        stageBorderMaterial_.SetFloat("expandT", 0.0f);
        stageBorderMaterial_.SetFloat("alpha", 1.0f);
        stageBorderMaterial_.SetFloat("innerMargin", 0.0f);

        grid_.Initialize(factory_, "bgGrid_select");

        // 案内の画像（元のシーン保存で表示していた 2 枚：ボタンの 13 コマ目と文字の 2 コマ目）
        Td3UIImage@ button = CreateTd3UIImage(owner, "UI/UI_Button.png", "StageSelectUIButton12");
        button.SetVerticalFrame(14, 12);
        button.SetAnchor(UIAnchor::Center);
        button.SetAnchoredPosition(Vector2(-891.6954345703125f, -373.8585205078125f));
        button.SetPivot(Vector2(0.5f, 0.5f));
        button.SetSize(Vector2(208.0f, 104.0f));
        button.SetSortOrder(50);

        Td3UIImage@ word = CreateTd3UIImage(owner, "UI/UI_word.png", "StageSelectUIWord1");
        word.SetVerticalFrame(6, 1);
        word.SetAnchor(UIAnchor::Center);
        word.SetAnchoredPosition(Vector2(-729.719482421875f, -371.48199462890625f));
        word.SetPivot(Vector2(0.5f, 0.5f));
        word.SetSize(Vector2(312.0f, 78.0f));
        word.SetSortOrder(50);
    }

    void Update()
    {
        const float dt = Time::DeltaTime();

        grid_.Update(dt, 1.5f, Vector2(0.0f, 0.0f));

        // 左スティックは倒した瞬間だけ 1 回動かす（戻すまで次を受けない）
        const float leftStickX = Input::GetLeftStick().x;
        int stickTriggeredDirection = 0;
        if (leftStickX <= -kSelectStickNavigateThreshold && gamepadMoveDirectionLatch_ != -1) {
            stickTriggeredDirection = -1;
            gamepadMoveDirectionLatch_ = -1;
        } else if (leftStickX >= kSelectStickNavigateThreshold && gamepadMoveDirectionLatch_ != 1) {
            stickTriggeredDirection = 1;
            gamepadMoveDirectionLatch_ = 1;
        } else if (abs(leftStickX) <= kSelectStickReleaseThreshold) {
            gamepadMoveDirectionLatch_ = 0;
        }

        const bool moveLeftTriggered = ActionTriggered(InputAction::SelectLeft) || stickTriggeredDirection < 0;
        const bool moveRightTriggered = ActionTriggered(InputAction::SelectRight) || stickTriggeredDirection > 0;

        if (!isMoving_ && moveLeftTriggered && GetStageNumber() > 1) {
            startSpriteOffset_ = currentSpriteOffsetX_;
            SetStageNumber(GetStageNumber() - 1);
            endSpriteOffset_ = stagePositionsX_[GetStageNumber() - 1];
            endLeftArrowAngle_ = kPi * 2.0f;
            isMoving_ = true;
            PlaySe("SE_Cursor.mp3");
        }
        if (!isMoving_ && moveRightTriggered && GetStageNumber() < kMaxStage) {
            startSpriteOffset_ = currentSpriteOffsetX_;
            SetStageNumber(GetStageNumber() + 1);
            endSpriteOffset_ = stagePositionsX_[GetStageNumber() - 1];
            endRightArrowAngle_ = -kPi * 2.0f;
            isMoving_ = true;
            PlaySe("SE_Cursor.mp3");
        }

        leftArrow_.SetActive(GetStageNumber() > 1);
        rightArrow_.SetActive(GetStageNumber() < kMaxStage);

        arrowAnimationTimer_ += dt;
        if (arrowAnimationTimer_ >= kSelectArrowAnimationDuration) {
            arrowAnimationTimer_ -= kSelectArrowAnimationDuration;
        }
        const float arrowAnimationT = Clamp(arrowAnimationTimer_ / kSelectArrowAnimationDuration, 0.0f, 1.0f);
        const float swingOffset = sin(arrowAnimationT * kPi * 2.0f) * kSelectArrowSwingDistance;

        if (isMoving_) {
            if (moveTimer_ < kSelectMoveDuration) {
                moveTimer_ += dt;
                const float t = Clamp(moveTimer_ / kSelectMoveDuration, 0.0f, 1.0f);
                currentSpriteOffsetX_ = Td3EaseLerp(startSpriteOffset_, endSpriteOffset_, t, Td3Ease::EaseOutQuad);
                leftArrow_.SetTranslate(-kSelectArrowOffsetX - swingOffset, kSelectArrowBaseY);
                rightArrow_.SetTranslate(kSelectArrowOffsetX + swingOffset, kSelectArrowBaseY);
                leftArrow_.SetRotateZ(Td3EaseLerp(0.0f, endLeftArrowAngle_, t, Td3Ease::EaseOutQuad));
                rightArrow_.SetRotateZ(Td3EaseLerp(0.0f, endRightArrowAngle_, t, Td3Ease::EaseOutQuad));
            } else {
                currentSpriteOffsetX_ = endSpriteOffset_;
                moveTimer_ = 0.0f;
                isMoving_ = false;
                endLeftArrowAngle_ = 0.0f;
                endRightArrowAngle_ = 0.0f;
            }
        } else {
            leftArrow_.SetTranslate(-kSelectArrowOffsetX - swingOffset, kSelectArrowBaseY);
            leftArrow_.SetRotateZ(0.0f);
            rightArrow_.SetTranslate(kSelectArrowOffsetX + swingOffset, kSelectArrowBaseY);
            rightArrow_.SetRotateZ(0.0f);
        }

        // ステージの絵を並べ、番号を絵の上端の上へ置く
        for (uint i = 0; i < stageSprites_.length(); i++) {
            const float relX = stagePositionsX_[i] - currentSpriteOffsetX_;
            Td3Sprite@ stage = stageSprites_[i];
            stage.SetTranslate(relX, stage.GetTranslate().y);
            const Vector2 stageSize = stage.GetActualSize();
            const float stageCentY = stage.GetTranslate().y;
            const float stageHalfH = stageSize.y * 0.5f;
            const Vector2 numSize = stageNumberSprites_[i].GetActualSize();
            const float numY = stageCentY + stageHalfH + numSize.y * 0.5f + kSelectNumberAboveMargin;
            stageNumberSprites_[i].SetTranslate(relX, numY);
            stageNumberIconSprites_[i].SetTranslate(relX, numY);
        }

        // 枠を選んでいるステージに合わせる
        {
            Td3Sprite@ stage = stageSprites_[GetStageNumber() - 1];
            const Vector2 stageActualSize = stage.GetActualSize();
            const float borderSpriteW = stageActualSize.x + kSelectBorderPaddingX * 2.0f;
            const float borderSpriteH = stageActualSize.y + kSelectBorderPadding * 2.0f;
            stageBorderSprite_.SetTranslate(stage.GetTranslate());
            stageBorderSprite_.SetScale(borderSpriteW, borderSpriteH);
            stageBorderSprite_.SetRotate(stage.GetRotate());
            const float innerMarginX = kSelectBorderPadding / borderSpriteW;
            const float innerMarginY = kSelectBorderPadding / borderSpriteH;
            stageBorderMaterial_.SetFloat("innerMargin", Min(innerMarginX, innerMarginY));
            stageBorderMaterial_.SetFloat("aspectRatio", borderSpriteW / borderSpriteH);

            borderLoopTimer_ += dt;
            if (borderLoopTimer_ >= borderLoopInterval_) {
                borderLoopTimer_ = 0.0f;
            }
            const float expandT = Clamp(borderLoopTimer_ / borderExpandDuration_, 0.0f, 1.0f);
            stageBorderMaterial_.SetFloat("expandT", expandT);
            stageBorderMaterial_.SetFloat("alpha", 1.0f);
        }

        // 選んでいるステージを脈打たせる（ほかのステージは選んでいた時の大きさのまま）
        if (stageAnimationTimer_ < kSelectStageAnimationDuration) {
            stageAnimationTimer_ += dt;
            const float t = Clamp(stageAnimationTimer_ / kSelectStageAnimationDuration, 0.0f, 1.0f);
            const float s1 = Td3EaseLerp(1.0f, kSelectMidStageAnimationScale, t, Td3Ease::EaseInOutSine);
            const float s2 = Td3EaseLerp(kSelectMidStageAnimationScale, 1.0f, t, Td3Ease::EaseInOutSine);
            const float s = Td3EaseLerp(s1, s2, t, Td3Ease::EaseInOutSine) * kSelectStageScale;
            stageSprites_[GetStageNumber() - 1].SetScale(s, s);
        } else {
            stageAnimationTimer_ = 0.0f;
        }

        backgroundEffect_.Update(dt, 0.0f);

        if (ActionTriggered(InputAction::Decide)) {
            PlaySe("SE_Decide.mp3");
            ChangeSceneTd3(kSceneGame);
        }
        if (ActionTriggered(InputAction::Back)) {
            PlaySe("SE_Decide.mp3");
            ChangeSceneTd3(kSceneTitle);
        }
    }
}
