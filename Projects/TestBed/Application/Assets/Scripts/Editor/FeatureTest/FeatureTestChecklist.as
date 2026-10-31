using namespace CoreEngine;

// テストの「確かめること」の一覧。各タブが 1 つずつ持ち、チェックを覚える。
// ウィンドウの公開メンバ変数の中にあるので、.as を保存して読み直しても、エディタを閉じて開き直しても残る。
class FeatureTestChecklist
{
    array<bool> done;

    // 一覧を描く。項目の数が変わったら、チェックの数を合わせる
    void Draw(const array<string> &in items)
    {
        if (done.length() != items.length()) {
            done.resize(items.length());
        }
        // ### の後ろを ID にすると、数が変わっても開閉が保たれる
        if (!EditorGUI::Foldout("確かめること（" + Count() + " / " + items.length() + "）###checks", true)) {
            return;
        }
        for (uint i = 0; i < items.length(); ++i) {
            EditorGUI::PushID(int(i));
            done[i] = EditorGUI::Toggle("##done", done[i]);
            EditorGUI::SameLine();
            if (done[i]) {
                EditorGUI::TextDisabled(items[i]);
            } else {
                EditorGUI::Label(items[i]);
            }
            EditorGUI::PopID();
        }
        EditorGUI::Separator();
    }

    int Count() const
    {
        int count = 0;
        for (uint i = 0; i < done.length(); ++i) {
            if (done[i]) {
                ++count;
            }
        }
        return count;
    }

    void Clear()
    {
        for (uint i = 0; i < done.length(); ++i) {
            done[i] = false;
        }
    }
}
