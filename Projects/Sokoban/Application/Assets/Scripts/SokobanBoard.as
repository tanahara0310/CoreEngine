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

    private int stage_ = 0;
    private int width_ = 0;
    private int height_ = 0;
    private array<bool> walls_;
    private array<bool> goals_;
    private array<GameObject@> boxes_;
    private array<GameObject@> spawned_;
    private GameObject@ player_;
    private int playerCell_ = 0;
    private int moves_ = 0;
    private bool cleared_ = false;
    private Vector4 boxColor_;

    void Start()
    {
        LoadStage(0);
    }

    void Update()
    {
        if (width_ == 0) {
            return;
        }
        if (Input::IsActionTriggered(InputAction::Restart)) {
            LoadStage(stage_);
            return;
        }
        if (cleared_) {
            if (Input::IsActionTriggered(InputAction::NextStage)) {
                LoadStage((stage_ + 1) % int(stages.length()));
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
                    playerCell_ = cell;
                }
            }
        }
        @player_ = Spawn(playerPrefab, playerCell_, "Player");

        for (int cell = 0; cell < cells; ++cell) {
            if (boxes_[cell] !is null) {
                boxColor_ = boxes_[cell].material.color;
                break;
            }
        }
        for (int cell = 0; cell < cells; ++cell) {
            PaintBox(cell);
        }

        stage_ = index;
        moves_ = 0;
        cleared_ = false;
        RefreshTexts();
    }

    // 1 マス進む。先が箱なら、その先が空いているときだけ押す
    private void TryMove(int dx, int dy)
    {
        const int next = Neighbor(playerCell_, dx, dy);
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
            MoveTo(box, beyond);
            PaintBox(beyond);
        }
        playerCell_ = next;
        MoveTo(player_, next);
        ++moves_;
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
            statusText.uiText.text = "STAGE " + (stage_ + 1) + " / " + stages.length() + "    手数 " + moves_;
        }
        if (messageText !is null) {
            if (!cleared_) {
                messageText.uiText.text = "";
            } else if (stage_ + 1 < int(stages.length())) {
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

    private void MoveTo(GameObject@ object, int cell)
    {
        const Vector3 position = object.transform.position;
        const float x = float(cell % width_) - float(width_ - 1) * 0.5f;
        const float z = float(height_ - 1) * 0.5f - float(cell / width_);
        object.transform.position = Vector3(x, position.y, z);
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
