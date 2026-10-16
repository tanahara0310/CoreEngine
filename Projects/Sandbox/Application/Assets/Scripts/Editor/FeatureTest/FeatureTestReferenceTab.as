using namespace CoreEngine;

// 「参照」のタブ。シーンのオブジェクトとアセットを選ぶ欄、画像の出し方、アセットの探し方
//
//   EditorGUI::ObjectField(ラベル, オブジェクト)    シーンのオブジェクトを選ぶ欄（Hierarchy からドラッグしても入る）。選んだものを返す
//   EditorGUI::AssetField(ラベル, パス, 種類)       アセットを選ぶ欄（Project からドラッグしても入る）。パスはプロジェクトの根から
//                                                  種類は Texture・Model・Audio・Prefab・Scene・Material・Json・Csv など（空ならすべて）
//   EditorGUI::Image(パス, 幅, 高さ)                画像を出す（幅か高さの片方が 0 なら、縦横の比を保つ）
//   EditorGUI::GetImageSize(パス)                  画像の縦横の画素数
//   EditorGUI::DrawImage(パス, 左上, 右下)          Canvas の中の決めた四角へ画像を描く
//   Editor::FindAssets(種類, フォルダ)              アセットのパスの一覧（フォルダはパスの先頭。空ならすべて）
// 選んだオブジェクトのハンドルは、そのオブジェクトが消えると isAlive が false になる。
class FeatureTestReferenceTab
{
    FeatureTestChecklist checks;
    GameObject@ target;
    string prefab = "Application/Assets/Prefabs/Physics/PhysicsBox.prefab";
    string model = "";
    string sound = "";
    string texture = "Application/Assets/Textures/circle.png";
    int assetType = 0;
    string folder = "Application/Assets";

    array<string> CheckItems()
    {
        array<string> items = {
            "オブジェクトの欄の右の印を押すと一覧から選べ、Hierarchy からドラッグしても入る",
            "「選んでいる物を入れる」で、Hierarchy で選んだオブジェクトが入る",
            "入れたオブジェクトの名前・親・子の数・位置・コンポーネントが出る。消すと「消えた」になる",
            "プレハブ・モデル・音・画像の欄は、その種類のアセットだけを一覧に出し、Project からドラッグしても入る",
            "画像が縦横の比を保って出て、画素数が出る",
            "描く場所に、同じ画像が四角と横長に引き伸ばした形で出る",
            "種類とフォルダを変えると、見つかったアセットの一覧と数が変わる"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("シーンのオブジェクト");
        @target = EditorGUI::ObjectField("オブジェクト", target);
        if (EditorGUI::Button("選んでいる物を入れる")) {
            @target = Editor::GetSelection();
        }
        if (target !is null) {
            if (!target.isAlive) {
                EditorGUI::HelpBox("入れたオブジェクトは消えました", EditorGUI::MessageType::Warning);
            } else {
                Transform@ parent = target.transform.parent;
                Vector3 position = target.transform.position;
                EditorGUI::Label("名前", target.name);
                EditorGUI::Label("親", parent is null ? "（なし）" : parent.gameObject.name);
                EditorGUI::Label("子の数", "" + target.GetChildren().length());
                EditorGUI::Label("位置", formatFloat(position.x, "", 0, 2) + ", " + formatFloat(position.y, "", 0, 2) + ", " + formatFloat(position.z, "", 0, 2));
                EditorGUI::Label("コンポーネント", Components(target));
            }
        }

        EditorGUI::Header("アセット");
        prefab = EditorGUI::AssetField("プレハブ", prefab, "Prefab");
        model = EditorGUI::AssetField("モデル", model, "Model");
        sound = EditorGUI::AssetField("音", sound, "Audio");
        texture = EditorGUI::AssetField("画像", texture, "Texture");

        if (texture != "") {
            Vector2 size = EditorGUI::GetImageSize(texture);
            EditorGUI::Label("画像の画素数", formatFloat(size.x, "", 0, 0) + " x " + formatFloat(size.y, "", 0, 0));
            EditorGUI::Image(texture, 0, 96);
            EditorGUI::Canvas(0, 100);
            EditorGUI::DrawImage(texture, Vector2(8.0f, 8.0f), Vector2(92.0f, 92.0f));
            EditorGUI::DrawImage(texture, Vector2(108.0f, 30.0f), Vector2(300.0f, 70.0f));
            EditorGUI::DrawRectOutline(Vector2(108.0f, 30.0f), Vector2(300.0f, 70.0f), Vector4(1.0f, 1.0f, 1.0f, 0.3f));
        }

        EditorGUI::Header("アセットを探す");
        array<string> types = { "", "Prefab", "Texture", "Model", "Audio", "Scene", "Json" };
        array<string> typeNames = { "すべて", "Prefab", "Texture", "Model", "Audio", "Scene", "Json" };
        assetType = EditorGUI::Popup("種類", Clamp(assetType, 0, int(types.length()) - 1), typeNames);
        folder = EditorGUI::TextField("フォルダ", folder);
        array<string>@ found = Editor::FindAssets(types[assetType], folder);
        EditorGUI::Label("見つかった数", "" + found.length());
        if (EditorGUI::BeginChild("found", 0, 120)) {
            for (uint i = 0; i < found.length() && i < 200; ++i) {
                EditorGUI::TextDisabled(found[i]);
            }
        }
        EditorGUI::EndChild();
    }

    private string Components(GameObject@ object)
    {
        array<string> names = { "MeshRenderer", "Rigidbody", "Collider", "Light", "Camera", "AudioSource", "FeatureTestTarget" };
        string result = "";
        for (uint i = 0; i < names.length(); ++i) {
            if (object.HasComponent(names[i])) {
                result += (result == "" ? "" : "・") + names[i];
            }
        }
        return result == "" ? "（調べた中にはなし）" : result;
    }
}
