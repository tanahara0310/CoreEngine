#include "pch.h"
#include "ShaderGraphSelfTest.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <algorithm>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        constexpr int kTestNode = 2;
        constexpr int kSampleNode = 50;

        void Connect(Graph& graph, int from, std::string_view fromPin, int to, std::string_view toPin)
        {
            graph.links.push_back(Link{ from, std::string(fromPin), to, std::string(toPin) });
        }

        void Add(std::vector<SelfTestCase>& cases, const Graph& graph, const std::string& name, int previewNode,
            std::string_view previewPin)
        {
            const std::string fullName = (graph.target == Target::PostEffect ? "Post_" : "") + name;
            GenerateOptions options;
            options.sourcePath = "SelfTest/" + fullName + ".shadergraph";
            options.previewNode = previewNode;
            options.previewPin = std::string(previewPin);
            cases.push_back(SelfTestCase{ fullName, graph.target, Generate(graph, options) });
        }

        /// @brief 試すノードを 1 つ置き、Dynamic の入力へ幅 width の定数をつなぐ（0 ならつながない）
        Graph MakeNodeGraph(Target target, const NodeDesc& desc, int width)
        {
            Graph graph = MakeEmptyGraph(target);
            Node node;
            node.id = kTestNode;
            node.type = std::string(desc.type);
            if (desc.type == "Swizzle") {
                // 入力の幅に収まる並び（逆順）にする
                std::string mask = std::string("xyzw").substr(0, static_cast<size_t>((std::max)(1, width)));
                std::reverse(mask.begin(), mask.end());
                node.settings["Mask"] = mask;
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
                Connect(graph, constant.id, "Out", kTestNode, desc.inputs[index].name);
            }
            return graph;
        }
    }

    /// @brief 目標 1 つ分の検査のグラフを足す
    static void AddTargetCases(std::vector<SelfTestCase>& cases, Target target)
    {
        Add(cases, MakeEmptyGraph(target), "Empty", 0, "");

        for (const NodeDesc& desc : AllNodes()) {
            if (!desc.creatable || !desc.Supports(target)) {
                continue;
            }
            const bool hasDynamic = std::any_of(desc.inputs.begin(), desc.inputs.end(),
                [](const PinDesc& pin) { return pin.type == ValueType::Dynamic; });
            const int maxWidth = hasDynamic ? 4 : 0;
            for (int width = 0; width <= maxWidth; ++width) {
                const Graph base = MakeNodeGraph(target, desc, width);
                for (size_t output = 0; output < desc.outputs.size(); ++output) {
                    const PinDesc& pin = desc.outputs[output];
                    const std::string name = std::string(desc.type) + "_w" + std::to_string(width) + "_o" + std::to_string(output);
                    Graph graph = base;
                    if (pin.type == ValueType::Texture2D || pin.type == ValueType::SamplerState) {
                        // テクスチャとサンプラーは Sample Texture 2D を通して色にする
                        Node sample;
                        sample.id = kSampleNode;
                        sample.type = "SampleTexture2D";
                        graph.nodes.push_back(sample);
                        Connect(graph, kTestNode, pin.name, kSampleNode, pin.type == ValueType::Texture2D ? "Texture" : "Sampler");
                        Add(cases, graph, name + "_preview", kSampleNode, "RGBA");
                        Connect(graph, kSampleNode, "RGBA", 1, "Base Color");
                        Connect(graph, kSampleNode, "A", 1, "Alpha");
                        Add(cases, graph, name + "_master", 0, "");
                        continue;
                    }
                    Add(cases, graph, name + "_preview", kTestNode, pin.name);
                    Connect(graph, kTestNode, pin.name, 1, "Base Color");
                    Connect(graph, kTestNode, pin.name, 1, "Alpha");
                    Add(cases, graph, name + "_master", 0, "");
                }
            }
        }

        // プロパティの種類ごと（1 つずつと、全種類を足し合わせたもの）
        constexpr PropertyType kTypes[] = { PropertyType::Float, PropertyType::Vector2, PropertyType::Vector3,
            PropertyType::Vector4, PropertyType::Color, PropertyType::Texture2D };
        Graph all = MakeEmptyGraph(target);
        int nextId = 2;
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
            node.id = kTestNode;
            node.type = "Property";
            node.property = property.id;
            single.nodes.push_back(node);
            if (type == PropertyType::Texture2D) {
                Node sample;
                sample.id = kSampleNode;
                sample.type = "SampleTexture2D";
                single.nodes.push_back(sample);
                Connect(single, kTestNode, "Out", kSampleNode, "Texture");
                Connect(single, kSampleNode, "RGBA", 1, "Base Color");
            } else {
                Connect(single, kTestNode, "Out", 1, "Base Color");
                Connect(single, kTestNode, "Out", 1, "Alpha");
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
        Connect(all, sum, "Out", 1, "Base Color");
        Add(cases, all, "Property_All", 0, "");

        // Custom Function（全部の種類のピン・同じ関数を 2 つのノードで使う）
        Graph function = MakeEmptyGraph(target);
        for (const int id : { kTestNode, kTestNode + 1 }) {
            Node node;
            node.id = id;
            node.type = std::string(kCustomFunctionType);
            node.settings["Name"] = "SelfTestFunction";
            node.settings["Inputs"] = "Float a;Vector2 b;Vector3 c;Vector4 d;Texture2D t;SamplerState s";
            node.settings["Outputs"] = "Float x;Vector2 y;Vector3 z;Vector4 w";
            node.settings["Body"] = "x = a;\ny = b;\n\nz = c + t.SampleLevel(s, b, 0.0f).rgb;\nw = d;";
            function.nodes.push_back(node);
        }
        Connect(function, kTestNode, "z", kTestNode + 1, "c");
        Connect(function, kTestNode + 1, "z", 1, "Base Color");
        Connect(function, kTestNode, "x", 1, "Alpha");
        Add(cases, function, "CustomFunction_All", 0, "");
    }

    std::vector<SelfTestCase> BuildSelfTestCases()
    {
        std::vector<SelfTestCase> cases;
        for (const Target target : { Target::Sprite, Target::PostEffect }) {
            AddTargetCases(cases, target);
        }
        return cases;
    }
}

#endif // CORE_EDITOR
