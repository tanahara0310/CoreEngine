// キャラクタの操作と、歩いて確かめるためのマップ
// 段差・坂・壁を並べ、人型を歩かせて当たりの具合を見る
[DisplayName("物理テスト：歩行")]
class CharacterLab : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("当たりと操作を持つキャラ本体")]
    string characterPrefab = "Application/Assets/Prefabs/Physics/Character.prefab";

    [Asset("Prefab")] [Tooltip("見た目の人型（キャラの子にする）")]
    string humanPrefab = "Application/Assets/Prefabs/Physics/HumanModel.prefab";

    [Asset("Prefab")] [Tooltip("床・段差・壁に使う板")]
    string platformPrefab = "Application/Assets/Prefabs/Physics/PhysicsPlatform.prefab";

    [Range(1.0f, 12.0f)] [Tooltip("歩く速さ m/s")]
    float walkSpeed = 3.5f;

    [Range(2.0f, 20.0f)] [Tooltip("走る速さ m/s")]
    float runSpeed = 7.0f;

    [Range(2.0f, 15.0f)] [Tooltip("跳び上がる速さ m/s")]
    float jumpSpeed = 5.5f;

    [Tooltip("歩行エリアの中心")]
    Vector3 areaCenter = Vector3(34.0f, 0.0f, 0.0f);

    private GameObject@ character_;
    private GameObject@ model_;
    private CharacterController@ controller_;
    private array<GameObject@> mapParts_;
    private Text3DRenderer@ statusText_;

    private float cameraYaw_ = 0.0f;
    private int serial_ = 0;

    void Start()
    {
        GameObject@ status = owner.FindObject("StatusBoard2");
        if (status !is null) {
            status.GetComponent(@statusText_);
        }

        BuildMap();
        SpawnCharacter();
    }

    void Update()
    {
        HandleInput();
        UpdateCamera();
        UpdateStatus();
    }

    // ===== マップ =====

    private void BuildMap()
    {
        const Vector3 c = areaCenter;

        // 床。上面を y=0 に合わせる
        Place(c + Vector3(0.0f, -0.25f, 0.0f), Vector3(34.0f, 0.5f, 26.0f));

        // 0.25m ずつ上がる階段（越えられる段差 0.3 の中）
        for (int step = 0; step < 4; ++step) {
            const float top = float(step + 1) * 0.25f;
            Place(c + Vector3(-13.0f + float(step) * 1.6f, top * 0.5f, -9.0f),
                  Vector3(1.6f, top, 5.0f));
        }
        // 階段を上りきった先の台
        Place(c + Vector3(-4.0f, 0.5f, -9.0f), Vector3(6.0f, 1.0f, 5.0f));

        // 越えられない高さの段差（0.6m）
        Place(c + Vector3(-13.0f, 0.3f, -2.0f), Vector3(3.0f, 0.6f, 4.0f));

        // 登れる坂（30 度）。下端が床とつながる高さに置く
        PlaceTilted(c + Vector3(5.0f, 2.25f, -9.0f), Vector3(9.0f, 0.4f, 5.0f), 0.5236f);

        // 滑り落ちる坂（60 度）
        PlaceTilted(c + Vector3(12.0f, 2.6f, -9.0f), Vector3(6.0f, 0.4f, 5.0f), 1.0472f);

        // L 字の壁。斜めに当たって沿って滑れるか見る
        Place(c + Vector3(0.0f, 1.0f, 11.0f), Vector3(20.0f, 2.0f, 0.5f));
        Place(c + Vector3(9.75f, 1.0f, 6.0f), Vector3(0.5f, 2.0f, 10.0f));

        // 飛び降りる高台
        Place(c + Vector3(-9.0f, 1.0f, 6.0f), Vector3(7.0f, 2.0f, 7.0f));

        // 通り抜ける細い隙間（肩幅 0.7 に対して 1.2）
        Place(c + Vector3(2.0f, 1.0f, 3.0f), Vector3(4.0f, 2.0f, 0.5f));
        Place(c + Vector3(2.0f, 1.0f, 5.7f), Vector3(4.0f, 2.0f, 0.5f));
    }

    private GameObject@ Place(const Vector3 &in center, const Vector3 &in size)
    {
        ++serial_;
        GameObject@ part = owner.InstantiatePrefab(platformPrefab, "Walk_" + serial_);
        if (part is null) {
            Warn("CharacterLab: 板を置けませんでした");
            return null;
        }
        part.transform.position = center;
        part.transform.scale = size;
        mapParts_.insertLast(part);
        return part;
    }

    private GameObject@ PlaceTilted(const Vector3 &in center, const Vector3 &in size, float pitch)
    {
        GameObject@ part = Place(center, size);
        if (part !is null) {
            part.transform.rotation = Vector3(0.0f, 0.0f, pitch);
        }
        return part;
    }

    // ===== キャラ =====

    private void SpawnCharacter()
    {
        @character_ = owner.InstantiatePrefab(characterPrefab, "Player");
        if (character_ is null) {
            Warn("CharacterLab: キャラを置けませんでした");
            return;
        }
        character_.transform.position = areaCenter + Vector3(0.0f, 1.0f, 0.0f);
        character_.GetComponent(@controller_);

        // 見た目は子にする。当たりは親のカプセルだけが持つ
        @model_ = owner.InstantiatePrefab(humanPrefab, "PlayerModel");
        if (model_ !is null) {
            model_.transform.SetParent(character_.transform);
            model_.transform.position = Vector3(0.0f, -0.9f, 0.0f);
        }
    }

    // ===== 操作 =====

    private void HandleInput()
    {
        if (Input::IsKeyTriggered(Key::V)) {
            g_characterMode = !g_characterMode;
        }
        if (!g_characterMode || controller_ is null || !controller_.exists) {
            return;
        }

        // カメラの向きを回す
        if (Input::IsKeyPressed(Key::Q)) { cameraYaw_ -= 2.0f * Time::DeltaTime(); }
        if (Input::IsKeyPressed(Key::E)) { cameraYaw_ += 2.0f * Time::DeltaTime(); }

        // 入力はカメラの向きを前として扱う
        float forwardInput = 0.0f;
        float rightInput = 0.0f;
        if (Input::IsKeyPressed(Key::W)) { forwardInput += 1.0f; }
        if (Input::IsKeyPressed(Key::S)) { forwardInput -= 1.0f; }
        if (Input::IsKeyPressed(Key::D)) { rightInput += 1.0f; }
        if (Input::IsKeyPressed(Key::A)) { rightInput -= 1.0f; }

        const Vector3 forward = Vector3(sin(cameraYaw_), 0.0f, cos(cameraYaw_));
        const Vector3 right = Vector3(cos(cameraYaw_), 0.0f, -sin(cameraYaw_));
        Vector3 move = forward * forwardInput + right * rightInput;

        const float length = sqrt(move.x * move.x + move.z * move.z);
        if (length > 0.0001f) {
            move = move * (1.0f / length);
            // 進む向きへ体を向ける
            character_.transform.rotation = Vector3(0.0f, atan2(move.x, move.z), 0.0f);
        }

        const float speed = Input::IsKeyPressed(Key::Shift) ? runSpeed : walkSpeed;
        controller_.SimpleMove(move * speed);

        if (Input::IsKeyTriggered(Key::Space)) {
            controller_.Jump(jumpSpeed);
        }

        // 落ちたら戻す
        if (character_.transform.position.y < -20.0f) {
            ResetCharacter();
        }
        if (Input::IsKeyTriggered(Key::F)) {
            ResetCharacter();
        }
    }

    private void ResetCharacter()
    {
        if (character_ is null) { return; }
        character_.transform.position = areaCenter + Vector3(0.0f, 1.0f, 0.0f);
        if (controller_ !is null && controller_.exists) {
            controller_.velocity = Vector3(0.0f, 0.0f, 0.0f);
        }
    }

    // ===== カメラ =====

    private void UpdateCamera()
    {
        if (!g_characterMode || character_ is null) { return; }

        GameCamera@ camera = Scene::GetGameCamera();
        if (camera is null || !camera.exists) { return; }

        const Vector3 target = character_.transform.position;
        const float distance = 8.0f;
        const float height = 3.2f;

        camera.position = target
            + Vector3(-sin(cameraYaw_) * distance, height, -cos(cameraYaw_) * distance);
        camera.rotation = Vector3(0.22f, cameraYaw_, 0.0f);
    }

    private void UpdateStatus()
    {
        if (statusText_ is null || !statusText_.exists) { return; }

        // 説明はキャラの頭の上に付いてくる
        GameObject@ board = statusText_.gameObject;
        if (board !is null && character_ !is null) {
            board.transform.position = character_.transform.position + Vector3(0.0f, 2.6f, 0.0f);
        }

        if (!g_characterMode) {
            statusText_.text = "V:キャラ操作に戻る";
            return;
        }

        string grounded = "空中";
        float height = 0.0f;
        if (controller_ !is null && controller_.exists) {
            grounded = controller_.grounded ? "接地" : "空中";
            height = character_.transform.position.y;
        }

        statusText_.text =
            "WASD:歩く  Shift:走る  Space:跳ぶ\n"
            + "Q/E:カメラ  F:戻す  V:物理デモへ\n"
            + grounded + "   高さ " + height;
    }
}

// PhysicsLab と操作キーを分け合うための切り替え（Space がどちらでも要るため）
bool g_characterMode = true;

bool IsCharacterMode()
{
    return g_characterMode;
}
