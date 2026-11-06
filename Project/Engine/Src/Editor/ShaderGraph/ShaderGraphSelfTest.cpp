#include "pch.h"
#include "ShaderGraphSelfTest.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <algorithm>
#include <map>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        constexpr int kMasterNode = 1;
        constexpr int kVertexMasterNode = 2;
        constexpr int kSampleNode = 50;

        /// @brief 試すノードの番号（3D は 2 番が頂点の出口）
        int TestNode(Target target)
        {
            return Is3D(target) ? 3 : 2;
        }

        void Connect(Graph& graph, int from, std::string_view fromPin, int to, std::string_view toPin)
        {
            graph.links.push_back(Link{ from, std::string(fromPin), to, std::string(toPin) });
        }

        const char* Prefix(Target target)
        {
            switch (target) {
            case Target::PostEffect: return "Post_";
            case Target::Lit: return "Lit_";
            case Target::Unlit: return "Unlit_";
            default: return "";
            }
        }

        void Add(std::vector<SelfTestCase>& cases, const Graph& graph, const std::string& name, int previewNode,
            std::string_view previewPin)
        {
            const std::string fullName = Prefix(graph.target) + name;
            GenerateOptions options;
            options.sourcePath = "SelfTest/" + fullName + ".shadergraph";
            options.previewNode = previewNode;
            options.previewPin = std::string(previewPin);
            cases.push_back(SelfTestCase{ fullName, graph.target, Generate(graph, options), graph, options });
        }

        /// @brief Fragment の出口のうち、目標で使う欄へ全部つなぐ（スプライトとポストエフェクトは Base Color と Alpha）
        void ConnectFragment(Graph& graph, int from, std::string_view fromPin)
        {
            const NodeDesc* const master = FindNodeDesc(kMasterNodeType);
            for (const PinDesc& pin : master->inputs) {
                if (pin.Supports(graph.target)) {
                    Connect(graph, from, fromPin, kMasterNode, pin.name);
                }
            }
        }

        /// @brief Vertex の出口の 2 つの欄へつなぐ
        void ConnectVertex(Graph& graph, int from, std::string_view fromPin)
        {
            Connect(graph, from, fromPin, kVertexMasterNode, "Position");
            Connect(graph, from, fromPin, kVertexMasterNode, "Normal");
        }

        /// @brief 設定を変えて試すときの設定（無ければ既定）
        using SettingMap = std::map<std::string, std::string>;

        /// @brief 試すノードを 1 つ置き、Dynamic の入力へ幅 width の定数をつなぐ（0 ならつながない）
        Graph MakeNodeGraph(Target target, const NodeDesc& desc, int width, uint8_t stage, const SettingMap* settings)
        {
            Graph graph = MakeEmptyGraph(target);
            Node node;
            node.id = TestNode(target);
            node.type = std::string(desc.type);
            if (desc.type == "Swizzle") {
                // 入力の幅に収まる並び（逆順）にする
                std::string mask = std::string("xyzw").substr(0, static_cast<size_t>((std::max)(1, width)));
                std::reverse(mask.begin(), mask.end());
                node.settings["Mask"] = mask;
            }
            if (desc.type == "Transform") {
                // 段ごとに移せる空間の組にする
                node.settings["From"] = stage == kStageVertex ? "Object" : "World";
                node.settings["To"] = stage == kStageVertex ? "World" : "Tangent";
                node.settings["Type"] = stage == kStageVertex ? "Position" : "Direction";
            }
            if (settings) {
                for (const auto& [name, value] : *settings) {
                    node.settings[name] = value;
                }
            }
            graph.nodes.push_back(node);
            for (size_t index = 0; index < desc.inputs.size(); ++index) {
                if (desc.inputs[index].binding != PinBinding::RequiredTexture) {
                    continue;
                }
                // つながないとエラーになるテクスチャへ、テクスチャのプロパティをつなぐ
                Property property;
                property.id = graph.NextPropertyId();
                property.name = "prop" + std::string(desc.inputs[index].name);
                property.type = PropertyType::Texture2D;
                graph.properties.push_back(property);
                Node value;
                value.id = 40 + static_cast<int>(index);
                value.type = "Property";
                value.property = property.id;
                graph.nodes.push_back(value);
                Connect(graph, value.id, "Out", node.id, desc.inputs[index].name);
            }
            if (width == 0) {
                return graph;
            }
            for (size_t index = 0; index < desc.inputs.size(); ++index) {
                if (desc.inputs[index].type != ValueType::Dynamic) {
                    continue;
                }
                Node constant;
                constant.id = 10 + static_cast<int>(index);
                constant.type = width == 1 ? "Float" : "Vector" + std::to_string(width);
                constant.values["X"] = { 0.25f, 0.0f, 0.0f, 0.0f };
                constant.values["Y"] = { 0.5f, 0.0f, 0.0f, 0.0f };
                graph.nodes.push_back(constant);
                Connect(graph, constant.id, "Out", node.id, desc.inputs[index].name);
            }
            return graph;
        }

        /// @brief ノード 1 種類を、出力ごとに映す形と出口へつなぐ形で試す
        /// @param settings 変える設定（無ければ既定）
        /// @param suffix 設定を変えたときに名前へ足す文字
        void AddNodeCases(std::vector<SelfTestCase>& cases, Target target, const NodeDesc& desc, uint8_t stage,
            const SettingMap* settings = nullptr, std::string_view suffix = {})
        {
            const bool vertex = stage == kStageVertex;
            const int test = TestNode(target);
            const bool hasDynamic = std::any_of(desc.inputs.begin(), desc.inputs.end(),
                [](const PinDesc& pin) { return pin.type == ValueType::Dynamic; });
            const int maxWidth = hasDynamic ? 4 : 0;
            for (int width = 0; width <= maxWidth; ++width) {
                const Graph base = MakeNodeGraph(target, desc, width, stage, settings);
                for (size_t output = 0; output < desc.outputs.size(); ++output) {
                    const PinDesc& pin = desc.outputs[output];
                    const std::string name = std::string(desc.type) + std::string(suffix) + "_w" + std::to_string(width)
                        + "_o" + std::to_string(output);
                    Graph graph = base;
                    if (pin.type == ValueType::Texture2D || pin.type == ValueType::SamplerState) {
                        // テクスチャとサンプラーは Sample Texture 2D（Vertex は LOD）を通して色にする
                        Node sample;
                        sample.id = kSampleNode;
                        sample.type = vertex ? "SampleTexture2DLOD" : "SampleTexture2D";
                        graph.nodes.push_back(sample);
                        Connect(graph, test, pin.name, kSampleNode, pin.type == ValueType::Texture2D ? "Texture" : "Sampler");
                        if (vertex) {
                            ConnectVertex(graph, kSampleNode, "RGBA");
                            Add(cases, graph, name + "_vertex", 0, "");
                            continue;
                        }
                        Add(cases, graph, name + "_preview", kSampleNode, "RGBA");
                        Connect(graph, kSampleNode, "RGBA", kMasterNode, "Base Color");
                        Connect(graph, kSampleNode, "A", kMasterNode, "Alpha");
                        Add(cases, graph, name + "_master", 0, "");
                        continue;
                    }
                    if (vertex) {
                        ConnectVertex(graph, test, pin.name);
                        Add(cases, graph, name + "_vertex", 0, "");
                        continue;
                    }
                    Add(cases, graph, name + "_preview", test, pin.name);
                    ConnectFragment(graph, test, pin.name);
                    Add(cases, graph, name + "_master", 0, "");
                }
            }
        }
    }

    /// @brief 目標 1 つ分の検査のグラフを足す
    static void AddTargetCases(std::vector<SelfTestCase>& cases, Target target)
    {
        const int test = TestNode(target);
        Add(cases, MakeEmptyGraph(target), "Empty", 0, "");

        for (const NodeDesc& desc : AllNodes()) {
            if (!desc.creatable || !desc.Supports(target)) {
                continue;
            }
            if (!Is3D(target) || desc.SupportsStage(kStageFragment)) {
                AddNodeCases(cases, target, desc, kStageFragment);
            }
            if (Is3D(target) && desc.SupportsStage(kStageVertex)) {
                AddNodeCases(cases, target, desc, kStageVertex);
            }
        }

        // 既定でない設定の形（物体の空間・Transform の全部の組・Triplanar の法線・深度の形）
        struct Variant
        {
            std::string_view type;
            SettingMap settings;
            std::string suffix;
            bool only3D = true;
        };
        std::vector<Variant> variants = {
            { "Position", { { "Space", "Object" } }, "_Object" },
            { "NormalVector", { { "Space", "Object" } }, "_Object" },
            { "TangentVector", { { "Space", "Object" } }, "_Object" },
            { "Triplanar", { { "Type", "Normal" } }, "_Normal" },
            { "SceneDepth", { { "Mode", "Raw" } }, "_Raw", false },
            { "SceneDepth", { { "Mode", "Eye" } }, "_Eye", false },
            { "SceneDepthDifference", { { "Mode", "Eye" } }, "_Eye", false },
        };
        for (const char* from : { "Object", "World", "Tangent" }) {
            for (const char* to : { "Object", "World", "Tangent" }) {
                for (const char* kind : { "Position", "Direction" }) {
                    if (std::string_view(from) != to) {
                        variants.push_back({ "Transform", { { "From", from }, { "To", to }, { "Type", kind } },
                            std::string("_") + from + to + kind });
                    }
                }
            }
        }
        for (const Variant& variant : variants) {
            const NodeDesc* const desc = FindNodeDesc(variant.type);
            if (!desc || !desc->Supports(target) || (variant.only3D && !Is3D(target))) {
                continue;
            }
            for (const uint8_t stage : { kStageFragment, kStageVertex }) {
                if (Is3D(target) ? desc->SupportsStage(stage) : stage == kStageFragment) {
                    AddNodeCases(cases, target, *desc, stage, &variant.settings, variant.suffix);
                }
            }
        }

        // プロパティの種類ごと（1 つずつと、全種類を足し合わせたもの）
        constexpr PropertyType kTypes[] = { PropertyType::Float, PropertyType::Vector2, PropertyType::Vector3,
            PropertyType::Vector4, PropertyType::Color, PropertyType::Texture2D };
        Graph all = MakeEmptyGraph(target);
        int nextId = all.NextNodeId();
        int sum = 0;
        for (const PropertyType type : kTypes) {
            Property property;
            property.id = all.NextPropertyId();
            property.name = std::string("prop") + ToString(type);
            property.type = type;
            all.properties.push_back(property);

            Graph single = MakeEmptyGraph(target);
            single.properties.push_back(property);
            Node node;
            node.id = test;
            node.type = "Property";
            node.property = property.id;
            single.nodes.push_back(node);
            if (type == PropertyType::Texture2D) {
                Node sample;
                sample.id = kSampleNode;
                sample.type = "SampleTexture2D";
                single.nodes.push_back(sample);
                Connect(single, test, "Out", kSampleNode, "Texture");
                Connect(single, kSampleNode, "RGBA", kMasterNode, "Base Color");
                if (Is3D(target)) {
                    Node lod;
                    lod.id = kSampleNode + 1;
                    lod.type = "SampleTexture2DLOD";
                    single.nodes.push_back(lod);
                    Connect(single, test, "Out", lod.id, "Texture");
                    ConnectVertex(single, lod.id, "RGBA");
                }
            } else {
                Connect(single, test, "Out", kMasterNode, "Base Color");
                Connect(single, test, "Out", kMasterNode, "Alpha");
                if (Is3D(target)) {
                    ConnectVertex(single, test, "Out");
                }
            }
            Add(cases, single, std::string("Property_") + ToString(type), 0, "");

            node.id = nextId++;
            all.nodes.push_back(node);
            const int value = node.id;
            if (type == PropertyType::Texture2D) {
                Node sample;
                sample.id = nextId++;
                sample.type = "SampleTexture2D";
                all.nodes.push_back(sample);
                Connect(all, value, "Out", sample.id, "Texture");
                Node multiply;
                multiply.id = nextId++;
                multiply.type = "Multiply";
                all.nodes.push_back(multiply);
                Connect(all, sample.id, "RGBA", multiply.id, "A");
                if (sum != 0) {
                    Connect(all, sum, "Out", multiply.id, "B");
                }
                sum = multiply.id;
                continue;
            }
            Node add;
            add.id = nextId++;
            add.type = "Add";
            all.nodes.push_back(add);
            Connect(all, value, "Out", add.id, "A");
            if (sum != 0) {
                Connect(all, sum, "Out", add.id, "B");
            }
            sum = add.id;
        }
        Connect(all, sum, "Out", kMasterNode, "Base Color");
        Add(cases, all, "Property_All", 0, "");

        // Custom Function（全部の種類のピン・同じ関数を 2 つのノードで使う。3D は Vertex でも使う）
        Graph function = MakeEmptyGraph(target);
        for (const int id : { test, test + 1 }) {
            Node node;
            node.id = id;
            node.type = std::string(kCustomFunctionType);
            node.settings["Name"] = "SelfTestFunction";
            node.settings["Inputs"] = "Float a;Vector2 b;Vector3 c;Vector4 d;Texture2D t;SamplerState s";
            node.settings["Outputs"] = "Float x;Vector2 y;Vector3 z;Vector4 w";
            node.settings["Body"] = "x = a;\ny = b;\n\nz = c + t.SampleLevel(s, b, 0.0f).rgb;\nw = d;";
            function.nodes.push_back(node);
        }
        Connect(function, test, "z", test + 1, "c");
        Connect(function, test + 1, "z", kMasterNode, "Base Color");
        Connect(function, test, "x", kMasterNode, "Alpha");
        if (Is3D(target)) {
            Connect(function, test + 1, "z", kVertexMasterNode, "Position");
        }
        Add(cases, function, "CustomFunction_All", 0, "");

        // Redirect にテクスチャを通す
        Graph redirect = MakeEmptyGraph(target);
        {
            Property property;
            property.id = redirect.NextPropertyId();
            property.name = "propTexture";
            property.type = PropertyType::Texture2D;
            redirect.properties.push_back(property);
            Node value;
            value.id = test;
            value.type = "Property";
            value.property = property.id;
            redirect.nodes.push_back(value);
            Node relay;
            relay.id = test + 1;
            relay.type = std::string(kRedirectNodeType);
            redirect.nodes.push_back(relay);
            Node sample;
            sample.id = kSampleNode;
            sample.type = "SampleTexture2D";
            redirect.nodes.push_back(sample);
            Connect(redirect, test, "Out", relay.id, "In");
            Connect(redirect, relay.id, "Out", kSampleNode, "Texture");
            Connect(redirect, kSampleNode, "RGBA", kMasterNode, "Base Color");
        }
        Add(cases, redirect, "Redirect_Texture", 0, "");

        // ノードのプレビュー（置けて映せる全部のノードを開き、上限の数ごとに 1 本にする）
        std::vector<const NodeDesc*> previewable;
        for (const NodeDesc& desc : AllNodes()) {
            if (desc.creatable && desc.Supports(target) && HasNodePreview(desc)
                && (!Is3D(target) || desc.SupportsStage(kStageFragment))) {
                previewable.push_back(&desc);
            }
        }
        for (size_t first = 0; first < previewable.size(); first += kNodePreviewMax) {
            Graph graph = MakeEmptyGraph(target);
            int id = graph.NextNodeId();
            const int firstId = id;
            const size_t last = (std::min)(previewable.size(), first + kNodePreviewMax);
            for (size_t index = first; index < last; ++index) {
                Node node;
                node.id = id++;
                node.type = std::string(previewable[index]->type);
                node.preview = true;
                if (node.type == "Swizzle") {
                    node.settings["Mask"] = "x";
                }
                if (node.type == "Transform") {
                    node.settings["From"] = "World";
                    node.settings["To"] = "Tangent";
                    node.settings["Type"] = "Direction";
                }
                graph.nodes.push_back(node);
            }
            // つながないとエラーになるテクスチャへ、テクスチャのプロパティをつなぐ
            for (size_t index = first; index < last; ++index) {
                const NodeDesc& desc = *previewable[index];
                for (const PinDesc& pin : desc.inputs) {
                    if (pin.binding != PinBinding::RequiredTexture) {
                        continue;
                    }
                    Property property;
                    property.id = graph.NextPropertyId();
                    property.name = "prop" + std::to_string(graph.properties.size());
                    property.type = PropertyType::Texture2D;
                    graph.properties.push_back(property);
                    Node value;
                    value.id = id++;
                    value.type = "Property";
                    value.property = property.id;
                    graph.nodes.push_back(value);
                    Connect(graph, value.id, "Out", firstId + static_cast<int>(index - first), pin.name);
                }
            }
            const std::string name = std::string(Prefix(target)) + "NodePreviews_" + std::to_string(first / kNodePreviewMax);
            GenerateOptions options;
            options.sourcePath = "SelfTest/" + name + ".shadergraph";
            const NodePreviewResult previews = GenerateNodePreviews(graph, options);
            SelfTestCase result{ name, target, {} };
            result.generated.hlsl = previews.hlsl;
            if (previews.hlsl.empty()) {
                result.generated.fatal = "ノードのプレビューを作れません";
            }
            for (const auto& [node, reason] : previews.unavailable) {
                result.generated.issues.push_back({ node, reason });
            }
            if (previews.tiles.size() != last - first) {
                result.generated.issues.push_back({ 0, "映さなかったノードがあります" });
            }
            cases.push_back(std::move(result));
        }
    }

    std::vector<SelfTestCase> BuildSelfTestCases()
    {
        std::vector<SelfTestCase> cases;
        for (const Target target : kAllTargetList) {
            AddTargetCases(cases, target);
        }
        return cases;
    }
}

#endif // CORE_EDITOR
