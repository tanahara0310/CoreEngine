// 文字で書いた盤面からステージを組み立て、プレイヤーの移動・箱押し・クリア判定を行う
// 盤面の記号：# 壁　. ゴール　$ 箱　* ゴール上の箱　@ プレイヤー　+ ゴール上のプレイヤー　空白 床
[DisplayName("倉庫番：盤面")]
class SokobanBoard : ScriptComponent
{
    [Tooltip("ステージの盤面。行を / で区切る")]
    array<string> stages = {
        "########/#      #/# @ $ .#/#      #/########",
        "#######/#. .  #/#  $  #/# $@  #/#     #/#######",
        "########/#  .   #/# $##$ #/#  @   #/###  . #/########",
        "#########/#   #   #/# $ . $ #/#.#@#.#.#/# $   $ #/#   #   #/#########"
    };

    [ObjectRef] [Tooltip("ステージ番号と手数を出す文字")]
    GameObject@ statusText;

    [ObjectRef] [Tooltip("クリアを知らせる文字")]
    GameObject@ messageText;

    [Asset("Prefab")] [Tooltip("床")]
    string floorPrefab = "Application/Assets/Prefabs/Floor.prefab";

    [Asset("Prefab")] [Tooltip("壁")]
    string wallPrefab = "Application/Assets/Prefabs/Wall.prefab";

    [Asset("Prefab")] [Tooltip("ゴール")]
    string goalPrefab = "Application/Assets/Prefabs/Goal.prefab";

    [Asset("Prefab")] [Tooltip("箱")]
    string boxPrefab = "Application/Assets/Prefabs/Box.prefab";

    [Asset("Prefab")] [Tooltip("プレイヤー")]
    string playerPrefab = "Application/Assets/Prefabs/Player.prefab";

    [Color] [Tooltip("ゴールに乗った箱の色")]
    Vector4 boxOnGoalColor = Vector4(0.3f, 0.8f, 0.35f, 1.0f);

    [Range(0.0f, 0.5f)] [Tooltip("1 マス動くのにかける秒数（0 なら一瞬で動く）")]
    float moveTime = 0.12f;

    [ObjectRef] [Tooltip("箱がゴールに乗ったときに出すパーティクル")]
    GameObject@ goalEffect;

    [ObjectRef] [Tooltip("クリアしたときに出す紙吹雪のパーティクル")]
    GameObject@ clearEffect;

    [Hidden]
    int stage = 0;

    [Hidden]
    int playerCell = 0;

    [Hidden]
    array<int> boxCells;

    [Hidden]
    int moves = 0;

    private int width_ = 0;
    private int height_ = 0;
    private array<bool> walls_;
    private array<bool> goals_;
    private array<GameObject@> boxes_;
    private array<GameObject@> spawned_;
    private GameObject@ player_;
    private bool cleared_ = false;
    private Vector4 boxColor_;

    void Start()
    {
        LoadStage(0);
    }

    // 置いた物を名前で探して消し、ステージを組み直してから、プレイヤーと箱を読み直す前の位置へ戻す
    void OnScriptReloaded()
    {
        const int savedPlayer = playerCell;
        const array<int> savedBoxes = boxCells;
        const int savedMoves = moves;

        const array<string> names = { "Floor", "Wall", "Goal", "Box", "Player" };
        for (uint i = 0; i < names.length(); ++i) {
            GameObject@ object = owner.FindObject(names[i]);
            while (object !is null) {
                object.Destroy();
                @object = owner.FindObject(names[i]);
            }
        }
        spawned_.resize(0);
        LoadStage(stage);
        RestoreState(savedPlayer, savedBoxes, savedMoves);
    }

    void Update()
    {
        if (width_ == 0) {
            return;
        }
        if (Input::IsActionTriggered(InputAction::Restart)) {
            LoadStage(stage);
            return;
        }
        if (cleared_) {
            if (Input::IsActionTriggered(InputAction::NextStage)) {
                LoadStage((stage + 1) % int(stages.length()));
            }
            return;
        }
        if (Input::IsActionTriggered(InputAction::MoveForward)) {
            TryMove(0, -1);
        } else if (Input::IsActionTriggered(InputAction::MoveBack)) {
            TryMove(0, 1);
        } else if (Input::IsActionTriggered(InputAction::MoveLeft)) {
            TryMove(-1, 0);
        } else if (Input::IsActionTriggered(InputAction::MoveRight)) {
            TryMove(1, 0);
        }
    }

    // 前のステージの物を消し、盤面の文字から床・壁・ゴール・箱・プレイヤーを並べる
    private void LoadStage(int index)
    {
        for (uint i = 0; i < spawned_.length(); ++i) {
            if (spawned_[i] !is null && spawned_[i].isAlive) {
                spawned_[i].Destroy();
            }
        }
        spawned_.resize(0);
        @player_ = null;
        width_ = 0;
        if (index < 0 || index >= int(stages.length())) {
            return;
        }

        const array<string> rows = SplitRows(stages[index]);
        height_ = int(rows.length());
        int width = 0;
        for (uint r = 0; r < rows.length(); ++r) {
            if (int(rows[r].length()) > width) {
                width = int(rows[r].length());
            }
        }
        const int cells = width * height_;
        walls_.resize(cells);
        goals_.resize(cells);
        boxes_.resize(cells);
        width_ = width;

        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                const int cell = row * width_ + col;
                const string mark = col < int(rows[row].length()) ? rows[row].substr(col, 1) : " ";
                walls_[cell] = mark == "#";
                goals_[cell] = mark == "." || mark == "*" || mark == "+";
                @boxes_[cell] = null;
                if (walls_[cell]) {
                    Spawn(wallPrefab, cell, "Wall");
                    continue;
                }
                Spawn(floorPrefab, cell, "Floor");
                if (goals_[cell]) {
                    Spawn(goalPrefab, cell, "Goal");
                }
                if (mark == "$" || mark == "*") {
                    @boxes_[cell] = Spawn(boxPrefab, cell, "Box");
                }
                if (mark == "@" || mark == "+") {
                    playerCell = cell;
                }
            }
        }
        @player_ = Spawn(playerPrefab, playerCell, "Player");

        for (int cell = 0; cell < cells; ++cell) {
            if (boxes_[cell] !is null) {
                boxColor_ = boxes_[cell].material.color;
                break;
            }
        }
        for (int cell = 0; cell < cells; ++cell) {
            PaintBox(cell);
        }

        stage = index;
        moves = 0;
        SyncBoxCells();
        cleared_ = false;
        RefreshTexts();
    }

    // 1 マス進む。先が箱なら、その先が空いているときだけ押す
    private void TryMove(int dx, int dy)
    {
        const int next = Neighbor(playerCell, dx, dy);
        if (next < 0 || walls_[next]) {
            return;
        }
        GameObject@ box = boxes_[next];
        if (box !is null) {
            const int beyond = Neighbor(next, dx, dy);
            if (beyond < 0 || walls_[beyond] || boxes_[beyond] !is null) {
                return;
            }
            @boxes_[beyond] = box;
            @boxes_[next] = null;
            MoveTo(box, beyond, true);
            PaintBox(beyond);
            if (goals_[beyond]) {
                Emit(goalEffect, CellPosition(beyond, 0.5f), 40);
            }
        }
        playerCell = next;
        MoveTo(player_, next, true);
        ++moves;
        SyncBoxCells();
        cleared_ = IsCleared();
        if (cleared_) {
            Celebrate();
        }
        RefreshTexts();
    }

    // 箱のあるマスを控える
    private void SyncBoxCells()
    {
        boxCells.resize(0);
        for (uint cell = 0; cell < boxes_.length(); ++cell) {
            if (boxes_[cell] !is null) {
                boxCells.insertLast(int(cell));
            }
        }
    }

    // 組み直したステージの箱とプレイヤーを、控えた位置へ置き直す。箱の数が合わなければステージの最初のまま
    private void RestoreState(int player, const array<int> &in boxCellsToRestore, int moveCount)
    {
        array<GameObject@> boxes;
        for (uint cell = 0; cell < boxes_.length(); ++cell) {
            if (boxes_[cell] !is null) {
                boxes.insertLast(boxes_[cell]);
            }
        }
        if (boxes.length() != boxCellsToRestore.length() || player < 0 || player >= int(walls_.length()) || walls_[player]) {
            return;
        }
        for (uint i = 0; i < boxCellsToRestore.length(); ++i) {
            const int cell = boxCellsToRestore[i];
            if (cell < 0 || cell >= int(walls_.length()) || walls_[cell]) {
                return;
            }
        }
        for (uint cell = 0; cell < boxes_.length(); ++cell) {
            @boxes_[cell] = null;
        }
        for (uint i = 0; i < boxes.length(); ++i) {
            const int cell = boxCellsToRestore[i];
            @boxes_[cell] = boxes[i];
            MoveTo(boxes[i], cell);
        }
        for (uint cell = 0; cell < boxes_.length(); ++cell) {
            PaintBox(int(cell));
        }
        playerCell = player;
        MoveTo(player_, playerCell);
        moves = moveCount;
        SyncBoxCells();
        cleared_ = IsCleared();
        RefreshTexts();
    }

    // 全部のゴールに箱が乗っていればクリア
    private bool IsCleared()
    {
        for (uint cell = 0; cell < goals_.length(); ++cell) {
            if (goals_[cell] && boxes_[cell] is null) {
                return false;
            }
        }
        return true;
    }

    private void RefreshTexts()
    {
        if (statusText !is null) {
            statusText.uiText.text = "STAGE " + (stage + 1) + " / " + stages.length() + "    手数 " + moves;
        }
        if (messageText !is null) {
            if (!cleared_) {
                messageText.uiText.text = "";
            } else if (stage + 1 < int(stages.length())) {
                messageText.uiText.text = "CLEAR!  Space で次のステージへ";
            } else {
                messageText.uiText.text = "ALL CLEAR!  Space で最初から";
            }
        }
    }

    // ゴールに乗った箱だけ色を変える
    private void PaintBox(int cell)
    {
        GameObject@ box = boxes_[cell];
        if (box !is null) {
            box.material.color = goals_[cell] ? boxOnGoalColor : boxColor_;
        }
    }

    // プレハブを置く。高さはプレハブの値のまま、横の位置だけマスに合わせる
    private GameObject@ Spawn(const string &in prefab, int cell, const string &in name)
    {
        GameObject@ object = owner.InstantiatePrefab(prefab, name);
        if (object is null) {
            return null;
        }
        MoveTo(object, cell);
        spawned_.insertLast(object);
        return object;
    }

    // マスの位置へ動かす。animate なら moveTime 秒かけて、動き出しを速く止まり際をゆっくりにする
    private void MoveTo(GameObject@ object, int cell, bool animate = false)
    {
        Tween::KillByLink(object, true);
        const Vector3 target = CellPosition(cell, object.transform.position.y);
        if (!animate || moveTime <= 0.0f) {
            object.transform.position = target;
            return;
        }
        Tween::MoveTo(object, target, moveTime).SetEase(EaseType::EaseOutQuad).SetLink(object);
    }

    // マスの中心の位置（高さは y）
    private Vector3 CellPosition(int cell, float y)
    {
        const float x = float(cell % width_) - float(width_ - 1) * 0.5f;
        const float z = float(height_ - 1) * 0.5f - float(cell / width_);
        return Vector3(x, y, z);
    }

    // 盤面の上の左右 2 か所で紙吹雪をはじけさせる
    private void Celebrate()
    {
        const float x = float(width_) * 0.25f;
        Emit(clearEffect, Vector3(-x, 3.0f, 0.0f), 160);
        Emit(clearEffect, Vector3(x, 3.0f, 0.0f), 160);
    }

    // パーティクルを出す場所へ動かし、行列を送り直してから粒を出す
    private void Emit(GameObject@ emitter, const Vector3 &in position, int count)
    {
        if (emitter is null) {
            return;
        }
        emitter.transform.position = position;
        emitter.transform.UpdateMatrix();
        ParticleSystem@ particles = emitter.particleSystem;
        if (particles.exists) {
            particles.Emit(count);
        }
    }

    // 隣のマス。盤面の外なら -1
    private int Neighbor(int cell, int dx, int dy)
    {
        const int col = cell % width_ + dx;
        const int row = cell / width_ + dy;
        if (col < 0 || col >= width_ || row < 0 || row >= height_) {
            return -1;
        }
        return row * width_ + col;
    }

    private array<string> SplitRows(const string &in data)
    {
        array<string> rows;
        uint start = 0;
        while (true) {
            const int slash = data.findFirst("/", start);
            if (slash < 0) {
                rows.insertLast(data.substr(start));
                break;
            }
            rows.insertLast(data.substr(start, slash - int(start)));
            start = uint(slash) + 1;
        }
        return rows;
    }
}
