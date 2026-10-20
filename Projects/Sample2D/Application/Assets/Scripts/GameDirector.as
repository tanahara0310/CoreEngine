using namespace CoreEngine;

// 見本の進行：集めたコインの数を出し、ゴールで終わり、R でやり直す。クリックした所に箱を落とす
[DisplayName("2D：進行")]
class GameDirector : ScriptComponent
{
    [ObjectRef] [Tooltip("集めたコインの数を出す文字")]
    GameObject@ coinText;

    [ObjectRef] [Tooltip("クリアを知らせる文字")]
    GameObject@ messageText;

    [Asset("Prefab")] [Tooltip("クリックで落とす箱")]
    string cratePrefab = "Application/Assets/Prefabs/Crate.prefab";

    [Range(1, 50)] [Tooltip("同時に置ける箱の数（超えたら古いものから消す）")]
    int maxCrates = 12;

    private int coins_ = 0;
    private int totalCoins_ = 0;
    private bool cleared_ = false;
    private array<GameObject@> crates_;

    void Update()
    {
        if (Input::IsActionTriggered(InputAction::Restart)) {
            // 斜めの境目で画面を覆ってから、同じシーンを開き直す（Project.json のポストエフェクト Wipe）
            SceneTransition wipe;
            wipe.type = TransitionType::Effect;
            wipe.effect = "Wipe";
            wipe.outSeconds = 0.35f;
            wipe.inSeconds = 0.35f;
            wipe.hideUI = true;
            Scene::ChangeScene(Scene::GetCurrentName(), wipe);
            return;
        }
        if (Input::IsMouseButtonTriggered(MouseButton::Left) && Input::IsPointerOverGame()) {
            DropCrate(Input::GetMousePosition());
        }
        RefreshText();
    }

    // コインが置かれたときに数える
    void RegisterCoin()
    {
        ++totalCoins_;
    }

    // コインを拾ったときに数える
    void CollectCoin()
    {
        ++coins_;
    }

    // ゴールに着いた
    void Clear()
    {
        if (cleared_) {
            return;
        }
        cleared_ = true;
        // 同じマテリアルのコインと旗を、まとめて速く光らせる（再生を止めると元に戻る）
        ShaderMaterial("Materials/Shine.mat").SetFloat("speed", 8.0f);

        // 画面の真ん中から光の輪を広げる（Project.json のポストエフェクト ClearFlash）
        PostEffect::SetEnabled("ClearFlash", true);
        Tween::To(0.0f, 1.0f, 0.8f, TweenFloatSetter(this.SetFlash))
            .OnComplete(TweenCallback(this.EndFlash))
            .SetLink(owner);
    }

    private void SetFlash(float progress)
    {
        PostEffect::GetMaterial("ClearFlash").SetFloat("progress", progress);
    }

    private void EndFlash()
    {
        PostEffect::SetEnabled("ClearFlash", false);
    }

    // 画面の点の、z = 0 の面の位置へ箱を落とす
    private void DropCrate(const Vector2 &in screen)
    {
        GameCamera@ camera = Scene::GetGameCamera();
        const float distance = -camera.position.z;
        Vector3 position = camera.ScreenToWorldPoint(Vector3(screen.x, screen.y, distance));
        position.z = 0.0f;

        GameObject@ crate = owner.InstantiatePrefab(cratePrefab, "Crate");
        if (crate is null) {
            return;
        }
        crate.transform.position = position;
        crates_.insertLast(crate);
        while (int(crates_.length()) > maxCrates) {
            crates_[0].Destroy();
            crates_.removeAt(0);
        }
    }

    private void RefreshText()
    {
        if (coinText !is null) {
            coinText.uiText.text = "コイン " + coins_ + " / " + totalCoins_;
        }
        if (messageText !is null) {
            if (!cleared_) {
                messageText.uiText.text = "";
            } else if (coins_ == totalCoins_) {
                messageText.uiText.text = "ALL CLEAR!  R でもう一度";
            } else {
                messageText.uiText.text = "CLEAR!  R でもう一度";
            }
        }
    }
}
