#pragma once

#ifdef CORE_EDITOR

#include "Utility/JsonManager/JsonManager.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief ピンの値の種類
    enum class ValueType : uint8_t
    {
        Float = 1,
        Vector2 = 2,
        Vector3 = 3,
        Vector4 = 4,
        Dynamic,      ///< つながった入力のうち最も幅の広いものに合わせる
        Texture2D,
        SamplerState,
    };

    /// @brief 数のピンか（テクスチャとサンプラー以外）
    constexpr bool IsNumeric(ValueType type)
    {
        return type != ValueType::Texture2D && type != ValueType::SamplerState;
    }

    /// @brief グラフの出口の種類
    enum class Target : uint8_t
    {
        Sprite,     ///< スプライトと UI 画像（ピクセルシェーダ）
        PostEffect, ///< プロジェクトのポストエフェクト（コンピュートシェーダ）
        Lit,        ///< 3D モデル（ライトで照らす・`名前.Surface.hlsl`）
        Unlit,      ///< 3D モデル（ライトで照らさない・`名前.Surface.hlsl`）
    };

    /// @brief 全部の出口の種類
    inline constexpr Target kAllTargetList[] = { Target::Sprite, Target::PostEffect, Target::Lit, Target::Unlit };

    /// @brief 3D モデルの出口か
    constexpr bool Is3D(Target target)
    {
        return target == Target::Lit || target == Target::Unlit;
    }

    /// @brief 出口の種類の名前（ファイルに書く綴り）
    const char* ToString(Target target);

    /// @brief 出口の種類の表示名
    const char* DisplayName(Target target);

    /// @brief Blackboard のプロパティの種類
    enum class PropertyType : uint8_t
    {
        Float,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Texture2D,
    };

    /// @brief プロパティの種類の名前（ファイルに書く綴り）
    const char* ToString(PropertyType type);

    /// @brief プロパティの種類の値の幅（テクスチャは 0）
    int WidthOf(PropertyType type);

    /// @brief Blackboard のプロパティ 1 つ（.mat の値・シェーダの定数）
    struct Property
    {
        std::string id;      ///< グラフの中で一意（名前を変えてもノードとのつながりが切れない）
        std::string name;    ///< シェーダの変数名と .mat のキー
        std::string display; ///< 表示名
        PropertyType type = PropertyType::Float;
        std::array<float, 4> value{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< 既定値
        bool hasRange = false;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
        bool hdr = false;
        std::string texture; ///< テクスチャの既定（`Application/Assets/…`）
        bool linear = false; ///< テクスチャを色でなくデータとして読む
    };

    /// @brief ノード 1 つ
    struct Node
    {
        int id = 0;
        std::string type;     ///< ノードの種類（`ShaderGraphNodeLibrary` の名前）
        float x = 0.0f;
        float y = 0.0f;
        std::string property; ///< Property ノードが指すプロパティの id
        std::map<std::string, std::array<float, 4>, std::less<>> values;  ///< つながっていない入力とノードの定数
        std::map<std::string, std::string, std::less<>> settings;         ///< 選択肢などの設定
        bool preview = false; ///< ノードの下のプレビューを開いているか
    };

    /// @brief ノードのまとまり（枠と名前を付けるだけで、生成には入らない）
    struct Group
    {
        int id = 0;
        std::string title;
        std::string color;     ///< `kGroupColors` の名前
        std::vector<int> nodes; ///< 中のノード（ノードは 1 つのグループにだけ入る）
    };

    /// @brief グループと付箋の色の名前
    inline constexpr std::string_view kGroupColors[] = { "Gray", "Blue", "Green", "Yellow", "Pink", "Purple" };

    /// @brief つながり 1 本（出力 → 入力）
    struct Link
    {
        int fromNode = 0;
        std::string fromPin;
        int toNode = 0;
        std::string toPin;

        bool operator==(const Link&) const = default;
    };

    /// @brief 出口のノードの種類（Fragment）
    inline constexpr std::string_view kMasterNodeType = "Master";

    /// @brief 3D の頂点の出口のノードの種類（Vertex）
    inline constexpr std::string_view kVertexMasterNodeType = "MasterVertex";

    /// @brief 出口のノード（Fragment か Vertex）か
    constexpr bool IsMasterNodeType(std::string_view type)
    {
        return type == kMasterNodeType || type == kVertexMasterNodeType;
    }

    /// @brief 付箋のノードの種類
    inline constexpr std::string_view kStickyNoteType = "StickyNote";

    /// @brief 線の途中の中継点のノードの種類
    inline constexpr std::string_view kRedirectNodeType = "Redirect";

    /// @brief グラフ全体（.shadergraph の中身）
    struct Graph
    {
        int version = 1;
        Target target = Target::Sprite;
        std::vector<Property> properties;
        std::vector<Node> nodes;
        std::vector<Link> links;
        std::vector<Group> groups;

        Node* FindNode(int id);
        const Node* FindNode(int id) const;
        Property* FindProperty(std::string_view id);
        const Property* FindProperty(std::string_view id) const;
        const Property* FindPropertyByName(std::string_view name) const;

        /// @brief 出口のノード（無ければ nullptr）
        const Node* FindMaster() const;

        /// @brief 頂点の出口のノード（無ければ nullptr）
        const Node* FindVertexMaster() const;

        /// @brief 入力ピンへつながっているつながり（無ければ nullptr）
        const Link* FindLinkTo(int node, std::string_view pin) const;

        /// @brief 次に使えるノードの番号
        int NextNodeId() const;

        /// @brief 次に使えるプロパティの id
        std::string NextPropertyId() const;

        /// @brief from から to へ出力をたどって行けるか（つなぐと循環するかの判定）
        bool Reaches(int from, int to) const;

        /// @brief ノードを消す（つながりとグループからも外す）
        void RemoveNode(int id);

        Group* FindGroup(int id);
        const Group* FindGroup(int id) const;

        /// @brief ノードの入っているグループ（無ければ nullptr）
        const Group* FindGroupOf(int node) const;

        /// @brief 次に使えるグループの番号
        int NextGroupId() const;

        /// @brief ノードをグループから外す（中のノードが無くなったグループは消す）
        void RemoveFromGroups(int node);
    };

    /// @brief 出口だけを置いた空のグラフ（3D は Fragment の出口が 1 番・Vertex の出口が 2 番）
    Graph MakeEmptyGraph(Target target);

    /// @brief JSON からグラフを読む
    /// @param warnings 読み飛ばしたもの
    /// @return 形が違って読めなければ false
    bool ReadGraph(const json& root, Graph& graph, std::vector<std::string>& warnings);

    /// @brief グラフを JSON にする
    json WriteGraph(const Graph& graph);

    /// @brief シェーダの名前（プロパティ・関数・関数の引数）として使えるか
    /// @param error 使えない訳
    bool IsValidIdentifier(std::string_view name, std::string* error);

    /// @brief プロパティの名前として使えるか
    /// @param error 使えない訳
    bool IsValidPropertyName(std::string_view name, std::string* error);
}

#endif // CORE_EDITOR
