#include "pch.h"
#include "ShaderGraphSelfTest.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <algorithm>

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
            cases.push_back(SelfTestCase{ fullName, graph.target, Generate(graph, options) });
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

        /// @brief 試すノードを 1 つ置き、Dynamic の入力へ幅 width の定数をつなぐ（0 ならつながない）
        Graph MakeNodeGraph(Target target, const NodeDesc& desc, int width, uint8_t stage)
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
            graph.nodes.push_back(node);
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
        void AddNodeCases(std::vector<SelfTestCase>& cases, Target target, const NodeDesc& desc, uint8_t stage)
        {
            const bool vertex = stage == kStageVertex;
            const int test = TestNode(target);
            const bool hasDynamic = std::any_of(desc.inputs.begin(), desc.inputs.end(),
                [](const PinDesc& pin) { return pin.type == ValueType::Dynamic; });
            const int maxWidth = hasDynamic ? 4 : 0;
            for (int width = 0; width <= maxWidth; ++width) {
                const Graph base = MakeNodeGraph(target, desc, width, stage);
                for (size_t output = 0; output < desc.outputs.size(); ++output) {
                    const PinDesc& pin = desc.outputs[output];
                    const std::string name = std::string(desc.type) + "_w" + std::to_string(width) + "_o" + std::to_string(output);
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
