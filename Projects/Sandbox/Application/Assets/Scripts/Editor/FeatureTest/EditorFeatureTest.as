// エディタの拡張でできることを、ひととおり試すウィンドウ。Tools > テスト > エディタ拡張のテスト で開く。
// 上の段のタブで分野を、下の段のタブで機能を選ぶ。各タブの使い方は、タブのクラスのファイル（同じフォルダ）の先頭に書いてある。
// 各タブの「確かめること」にチェックを入れると、上の合計に出る。チェックはウィンドウの値として保存し、次の起動でも残る。
//
//   画面の部品   文字（FeatureTestTextTab）・入力（FeatureTestInputTab）・並べ方（FeatureTestLayoutTab）・
//               一覧（FeatureTestListTab。一覧と表と木）・メニュー（FeatureTestPopupTab。ポップアップと確認ダイアログ）・
//               マウス（FeatureTestDeviceTab。マウスとキーと描く場所）・参照（FeatureTestReferenceTab。オブジェクトとアセットと画像）
//   大きな部品   ノード（FeatureTestNodeTab）・グラフとカーブ（FeatureTestGraphTab）・時間軸（FeatureTestTimelineTab）
//   シーン       オブジェクト（FeatureTestSceneTab。作る・消す・選ぶ・Undo）・シーンビュー（FeatureTestSceneViewTab）・
//               インスペクタ（FeatureTestInspectorTab。インスペクタの拡張とギズモ）・データ（FeatureTestDataTab。データファイル）
//   仕組み       ウィンドウ（FeatureTestWindowTab。メニューの関数と呼ばれる順番）・読み直し（FeatureTestReloadTab。読み直しと保存）・
//               誤った使い方（FeatureTestMisuseTab）
//
// タブのクラスはウィンドウのメンバ変数として持つ。公開メンバ変数の中のクラスは、中身ごと読み直しで持ち越し、起動をまたいで保存する。
[MenuItem("Tools/テスト/エディタ拡張のテスト")]
class EditorFeatureTest : EditorWindow
{
    FeatureTestTextTab text;
    FeatureTestInputTab input;
    FeatureTestLayoutTab layout;
    FeatureTestListTab list;
    FeatureTestPopupTab popup;
    FeatureTestDeviceTab device;
    FeatureTestReferenceTab reference;
    FeatureTestNodeTab node;
    FeatureTestGraphTab graph;
    FeatureTestTimelineTab timeline;
    FeatureTestSceneTab scene;
    FeatureTestSceneViewTab sceneView;
    FeatureTestInspectorTab inspector;
    FeatureTestDataTab data;
    FeatureTestWindowTab window;
    FeatureTestReloadTab reload;
    FeatureTestMisuseTab misuse;

    // 「読み直し」のタブで出す数。ウィンドウ直下の公開メンバ変数は起動をまたいで残り、private は読み直しの間だけ残る
    int windowPublicCount = 0;
    private int windowPrivateCount_ = 0;

    // 今開いている下の段のタブ（シーンビューのタブを開いている間だけ、シーンビューに描く）
    private string openTab_ = "";
    private int checkedCount_ = 0;
    private int checkTotal_ = 0;

    void OnEnable()
    {
        window.OnEnable();
    }

    void OnDisable()
    {
        window.OnDisable();
    }

    void OnGUI()
    {
        window.CountFrame();
        DrawSummary();

        openTab_ = "";
        if (EditorGUI::BeginTabBar("groups")) {
            if (EditorGUI::BeginTabItem("画面の部品")) {
                DrawWidgetTabs();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("大きな部品")) {
                DrawToolTabs();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("シーン")) {
                DrawSceneTabs();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("仕組み")) {
                DrawSystemTabs();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
        misuse.AfterTabs();
    }

    void OnSceneGUI()
    {
        if (openTab_ == "シーンビュー") {
            sceneView.OnSceneGUI();
        }
    }

    private void DrawWidgetTabs()
    {
        if (EditorGUI::BeginTabBar("widgets")) {
            if (BeginTab("文字")) {
                text.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("入力")) {
                input.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("並べ方")) {
                layout.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("一覧")) {
                list.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("メニュー")) {
                popup.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("マウス")) {
                device.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("参照")) {
                reference.Draw();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
    }

    private void DrawToolTabs()
    {
        if (EditorGUI::BeginTabBar("tools")) {
            if (BeginTab("ノード")) {
                node.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("グラフとカーブ")) {
                graph.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("時間軸")) {
                timeline.Draw();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
    }

    private void DrawSceneTabs()
    {
        if (EditorGUI::BeginTabBar("scene")) {
            if (BeginTab("オブジェクト")) {
                scene.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("シーンビュー")) {
                sceneView.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("インスペクタ")) {
                inspector.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("データ")) {
                data.Draw();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
    }

    private void DrawSystemTabs()
    {
        if (EditorGUI::BeginTabBar("system")) {
            if (BeginTab("ウィンドウ")) {
                window.Draw();
            }
            EditorGUI::EndTabItem();
            if (BeginTab("読み直し")) {
                if (reload.Draw(windowPublicCount, windowPrivateCount_)) {
                    ++windowPublicCount;
                    ++windowPrivateCount_;
                }
            }
            EditorGUI::EndTabItem();
            if (BeginTab("誤った使い方")) {
                misuse.Draw();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
    }

    // 下の段のタブを 1 つ始める。開いていれば名前を覚えて true（戻り値に関係なく EndTabItem と組にする）
    private bool BeginTab(const string &in label)
    {
        if (!EditorGUI::BeginTabItem(label)) {
            return false;
        }
        openTab_ = label;
        return true;
    }

    // 全部のタブのチェックを数え、合計を出す
    private void DrawSummary()
    {
        VisitChecklists(false);
        if (EditorGUI::Button("チェックを全部外す")) {
            EditorGUI::OpenPopup("チェックを全部外す");
        }
        EditorGUI::SameLine();
        float ratio = checkTotal_ == 0 ? 0.0f : float(checkedCount_) / float(checkTotal_);
        EditorGUI::ProgressBar(ratio, "確かめた項目 " + checkedCount_ + " / " + checkTotal_);

        if (EditorGUI::BeginPopupModal("チェックを全部外す")) {
            EditorGUI::Label("全部のタブの「確かめること」のチェックを外しますか？");
            if (EditorGUI::Button("外す", 100)) {
                VisitChecklists(true);
                EditorGUI::CloseCurrentPopup();
            }
            EditorGUI::SameLine();
            if (EditorGUI::Button("やめる", 100)) {
                EditorGUI::CloseCurrentPopup();
            }
        }
        EditorGUI::EndPopup();
    }

    private void VisitChecklists(bool clear)
    {
        checkedCount_ = 0;
        checkTotal_ = 0;
        Visit(text.checks, text.CheckItems(), clear);
        Visit(input.checks, input.CheckItems(), clear);
        Visit(layout.checks, layout.CheckItems(), clear);
        Visit(list.checks, list.CheckItems(), clear);
        Visit(popup.checks, popup.CheckItems(), clear);
        Visit(device.checks, device.CheckItems(), clear);
        Visit(reference.checks, reference.CheckItems(), clear);
        Visit(node.checks, node.CheckItems(), clear);
        Visit(graph.checks, graph.CheckItems(), clear);
        Visit(timeline.checks, timeline.CheckItems(), clear);
        Visit(scene.checks, scene.CheckItems(), clear);
        Visit(sceneView.checks, sceneView.CheckItems(), clear);
        Visit(inspector.checks, inspector.CheckItems(), clear);
        Visit(data.checks, data.CheckItems(), clear);
        Visit(window.checks, window.CheckItems(), clear);
        Visit(reload.checks, reload.CheckItems(), clear);
        Visit(misuse.checks, misuse.CheckItems(), clear);
    }

    private void Visit(FeatureTestChecklist@ checks, const array<string> &in items, bool clear)
    {
        if (clear) {
            checks.Clear();
        }
        checkedCount_ += Min(checks.Count(), int(items.length()));
        checkTotal_ += int(items.length());
    }
}
