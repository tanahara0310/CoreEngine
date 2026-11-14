#include "pch.h"
#include "ShaderGraph.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <tuple>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief プロパティの名前に使えない HLSL の予約語と、生成するコードが使う名前
        constexpr std::string_view kReservedNames[] = {
            "bool", "int", "uint", "float", "half", "double", "min16float", "vector", "matrix",
            "float2", "float3", "float4", "int2", "int3", "int4", "uint2", "uint3", "uint4",
            "true", "false", "if", "else", "for", "while", "do", "switch", "case", "default",
            "break", "continue", "return", "discard", "struct", "cbuffer", "register", "static",
            "const", "in", "out", "inout", "uniform", "void", "sampler", "texture", "Texture2D",
            "SamplerState", "ConstantBuffer", "main", "IN", "input", "output",
            "gMaterial", "gTexture", "gSampler", "gPointSampler", "gClampSampler", "gWrapSampler",
            "gSpriteDraw", "gSpriteTransform", "gOutput", "gPostEffect", "baseColor", "alpha",
            "id", "GraphInput", "GraphProperties", "SpriteUV", "SampleSprite", "SpriteColor", "SpriteDefaultColor",
            "IsOutsideScreen", "PostEffectUV", "LoadScreen", "SampleScreen",
            "gGraph", "OUT", "Surface", "SurfaceVertex", "gNormalMap", "gMetallicRoughnessMap", "gEmissiveMap", "gAOMap",
            "gCamera", "gLightCounts", "gDirectionalLights", "gPointLights", "gSpotLights", "gAreaLights",
            "gRTShadowMask", "gFog", "gVertexAnim", "gInstanceData", "gTransformationMatrix", "gSurfacePreview",
            "gPrevSkinnedPositions", "LoadPrevSkinnedPosition",
            "gSourceVertices", "gIndices", "gOutputVertices", "gDeform",
            "gSkyAmbient", "gSkyIrradianceSH", "gSkySpecularMap",
            "gVolumetricFogIntegrated", "gVolumetricFogFarLight", "gVolumetricFogSampler", "ApplyForwardFog",
        };

        bool ParseTarget(const std::string& text, Target& target)
        {
            for (const Target candidate : kAllTargetList) {
                if (text == ToString(candidate)) {
                    target = candidate;
                    return true;
                }
            }
            return false;
        }

        bool ParsePropertyType(const std::string& text, PropertyType& type)
        {
            for (const PropertyType candidate : { PropertyType::Float, PropertyType::Vector2, PropertyType::Vector3,
                                                  PropertyType::Vector4, PropertyType::Color, PropertyType::Texture2D,
                                                  PropertyType::SamplerState }) {
                if (text == ToString(candidate)) {
                    type = candidate;
                    return true;
                }
            }
            return false;
        }

        /// @brief 数 1〜4 個の配列を読む（足りない分は 0）
        std::array<float, 4> ReadNumbers(const json& value)
        {
            std::array<float, 4> numbers{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (value.is_number()) {
                numbers[0] = value.get<float>();
                return numbers;
            }
            if (!value.is_array()) {
                return numbers;
            }
            for (size_t index = 0; index < value.size() && index < numbers.size(); ++index) {
                if (value[index].is_number()) {
                    numbers[index] = value[index].get<float>();
                }
            }
            return numbers;
        }

        /// @brief 数を count 個の配列にする
        json WriteNumbers(const std::array<float, 4>& numbers, int count)
        {
            json array = json::array();
            for (int index = 0; index < count && index < 4; ++index) {
                array.push_back(numbers[static_cast<size_t>(index)]);
            }
            return array;
        }
    }

    const char* ToString(Target target)
    {
        switch (target) {
        case Target::Sprite: return "Sprite";
        case Target::PostEffect: return "PostEffect";
        case Target::Lit: return "Lit";
        case Target::Unlit: return "Unlit";
        }
        return "Sprite";
    }

    const char* DisplayName(Target target)
    {
        switch (target) {
        case Target::Sprite: return "スプライト／UI 画像";
        case Target::PostEffect: return "ポストエフェクト";
        case Target::Lit: return "3D（Lit）";
        case Target::Unlit: return "3D（Unlit）";
        }
        return "スプライト／UI 画像";
    }

    const char* ToString(PropertyType type)
    {
        switch (type) {
        case PropertyType::Float:     return "Float";
        case PropertyType::Vector2:   return "Vector2";
        case PropertyType::Vector3:   return "Vector3";
        case PropertyType::Vector4:   return "Vector4";
        case PropertyType::Color:     return "Color";
        case PropertyType::Texture2D: return "Texture2D";
        case PropertyType::SamplerState: return "SamplerState";
        }
        return "Float";
    }

    int WidthOf(PropertyType type)
    {
        switch (type) {
        case PropertyType::Float:     return 1;
        case PropertyType::Vector2:   return 2;
        case PropertyType::Vector3:   return 3;
        case PropertyType::Vector4:   return 4;
        case PropertyType::Color:     return 4;
        case PropertyType::Texture2D: return 0;
        case PropertyType::SamplerState: return 0;
        }
        return 1;
    }

    Node* Graph::FindNode(int id)
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; });
        return found == nodes.end() ? nullptr : &*found;
    }

    const Node* Graph::FindNode(int id) const
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; });
        return found == nodes.end() ? nullptr : &*found;
    }

    Property* Graph::FindProperty(std::string_view id)
    {
        const auto found = std::find_if(properties.begin(), properties.end(),
            [id](const Property& property) { return property.id == id; });
        return found == properties.end() ? nullptr : &*found;
    }

    const Property* Graph::FindProperty(std::string_view id) const
    {
        const auto found = std::find_if(properties.begin(), properties.end(),
            [id](const Property& property) { return property.id == id; });
        return found == properties.end() ? nullptr : &*found;
    }

    const Property* Graph::FindPropertyByName(std::string_view name) const
    {
        const auto found = std::find_if(properties.begin(), properties.end(),
            [name](const Property& property) { return property.name == name; });
        return found == properties.end() ? nullptr : &*found;
    }

    const Node* Graph::FindMaster() const
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(),
            [](const Node& node) { return node.type == kMasterNodeType; });
        return found == nodes.end() ? nullptr : &*found;
    }

    const Node* Graph::FindVertexMaster() const
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(),
            [](const Node& node) { return node.type == kVertexMasterNodeType; });
        return found == nodes.end() ? nullptr : &*found;
    }

    const Node* Graph::FindSubGraphOutput() const
    {
        const auto found = std::find_if(nodes.begin(), nodes.end(),
            [](const Node& node) { return node.type == kSubGraphOutputType; });
        return found == nodes.end() ? nullptr : &*found;
    }

    const Link* Graph::FindLinkTo(int node, std::string_view pin) const
    {
        const auto found = std::find_if(links.begin(), links.end(),
            [node, pin](const Link& link) { return link.toNode == node && link.toPin == pin; });
        return found == links.end() ? nullptr : &*found;
    }

    int Graph::NextNodeId() const
    {
        int next = 1;
        for (const Node& node : nodes) {
            next = (std::max)(next, node.id + 1);
        }
        return next;
    }

    std::string Graph::NextPropertyId() const
    {
        int next = 1;
        for (const Property& property : properties) {
            if (property.id.size() > 1 && property.id[0] == 'p') {
                const int number = std::atoi(property.id.c_str() + 1);
                next = (std::max)(next, number + 1);
            }
        }
        return "p" + std::to_string(next);
    }

    bool Graph::Reaches(int from, int to) const
    {
        std::vector<int> stack{ from };
        std::set<int> visited;
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            if (current == to) {
                return true;
            }
            if (!visited.insert(current).second) {
                continue;
            }
            for (const Link& link : links) {
                if (link.fromNode == current) {
                    stack.push_back(link.toNode);
                }
            }
        }
        return false;
    }

    void Graph::RemoveNode(int id)
    {
        std::erase_if(links, [id](const Link& link) { return link.fromNode == id || link.toNode == id; });
        std::erase_if(nodes, [id](const Node& node) { return node.id == id; });
        RemoveFromGroups(id);
    }

    Group* Graph::FindGroup(int id)
    {
        const auto found = std::find_if(groups.begin(), groups.end(), [id](const Group& group) { return group.id == id; });
        return found == groups.end() ? nullptr : &*found;
    }

    const Group* Graph::FindGroup(int id) const
    {
        const auto found = std::find_if(groups.begin(), groups.end(), [id](const Group& group) { return group.id == id; });
        return found == groups.end() ? nullptr : &*found;
    }

    const Group* Graph::FindGroupOf(int node) const
    {
        const auto found = std::find_if(groups.begin(), groups.end(), [node](const Group& group) {
            return std::find(group.nodes.begin(), group.nodes.end(), node) != group.nodes.end();
        });
        return found == groups.end() ? nullptr : &*found;
    }

    int Graph::NextGroupId() const
    {
        int next = 1;
        for (const Group& group : groups) {
            next = (std::max)(next, group.id + 1);
        }
        return next;
    }

    void Graph::RemoveFromGroups(int node)
    {
        for (Group& group : groups) {
            std::erase(group.nodes, node);
        }
        std::erase_if(groups, [](const Group& group) { return group.nodes.empty(); });
    }

    Graph MakeEmptyGraph(Target target)
    {
        Graph graph;
        graph.target = target;
        Node master;
        master.id = 1;
        master.type = std::string(kMasterNodeType);
        master.x = 400.0f;
        master.y = 0.0f;
        graph.nodes.push_back(std::move(master));
        if (Is3D(target)) {
            Node vertex;
            vertex.id = 2;
            vertex.type = std::string(kVertexMasterNodeType);
            vertex.x = 400.0f;
            vertex.y = -160.0f;
            graph.nodes.push_back(std::move(vertex));
        }
        return graph;
    }

    Graph MakeEmptySubGraph(Target previewTarget)
    {
        Graph graph;
        graph.target = previewTarget;
        graph.subGraph = true;
        Node output;
        output.id = 1;
        output.type = std::string(kSubGraphOutputType);
        output.x = 400.0f;
        output.y = 0.0f;
        output.settings["Outputs"] = std::string(kDefaultSubGraphOutputs);
        graph.nodes.push_back(std::move(output));
        return graph;
    }

    bool ReadGraph(const json& root, Graph& graph, std::vector<std::string>& warnings)
    {
        if (!root.is_object()) {
            return false;
        }
        graph = Graph{};
        graph.version = root.value("version", 1);
        if (const auto target = root.find("target"); target != root.end() && target->is_string()) {
            if (!ParseTarget(target->get<std::string>(), graph.target)) {
                warnings.push_back("知らない出口 " + target->get<std::string>() + " なので Sprite として読みました");
            }
        }
        if (const auto subGraph = root.find("subGraph"); subGraph != root.end() && subGraph->is_boolean()) {
            graph.subGraph = subGraph->get<bool>();
        }
        if (const auto category = root.find("category"); graph.subGraph && category != root.end() && category->is_string()) {
            graph.category = category->get<std::string>();
        }

        if (const auto properties = root.find("properties"); properties != root.end() && properties->is_array()) {
            for (const json& item : *properties) {
                if (!item.is_object()) {
                    continue;
                }
                Property property;
                property.id = item.value("id", std::string());
                property.name = item.value("name", std::string());
                property.display = item.value("display", property.name);
                if (!ParsePropertyType(item.value("type", std::string("Float")), property.type)
                    || (property.type == PropertyType::SamplerState && !graph.subGraph)) {
                    property.type = PropertyType::Float;
                    warnings.push_back("プロパティ " + property.name + " の種類が読めないので Float にしました");
                }
                if (const auto value = item.find("default"); value != item.end()) {
                    if (property.type == PropertyType::Texture2D) {
                        property.texture = value->is_string() ? value->get<std::string>() : std::string();
                    } else {
                        property.value = ReadNumbers(*value);
                    }
                }
                if (const auto range = item.find("range"); range != item.end() && range->is_array() && range->size() == 2) {
                    property.hasRange = true;
                    property.rangeMin = (*range)[0].is_number() ? (*range)[0].get<float>() : 0.0f;
                    property.rangeMax = (*range)[1].is_number() ? (*range)[1].get<float>() : 1.0f;
                }
                property.hdr = item.value("hdr", false);
                property.linear = item.value("linear", false);
                if (property.id.empty()) {
                    property.id = graph.NextPropertyId();
                }
                graph.properties.push_back(std::move(property));
            }
        }

        std::set<int> ids;
        if (const auto nodes = root.find("nodes"); nodes != root.end() && nodes->is_array()) {
            for (const json& item : *nodes) {
                if (!item.is_object()) {
                    continue;
                }
                Node node;
                node.id = item.value("id", 0);
                node.type = item.value("type", std::string());
                if (node.id <= 0 || node.type.empty() || !ids.insert(node.id).second) {
                    warnings.push_back("番号か種類の無いノード（または番号の重なったノード）を読み飛ばしました");
                    continue;
                }
                if (const auto pos = item.find("pos"); pos != item.end() && pos->is_array() && pos->size() == 2) {
                    node.x = (*pos)[0].is_number() ? (*pos)[0].get<float>() : 0.0f;
                    node.y = (*pos)[1].is_number() ? (*pos)[1].get<float>() : 0.0f;
                }
                node.property = item.value("property", std::string());
                if (const auto values = item.find("values"); values != item.end() && values->is_object()) {
                    for (const auto& [name, value] : values->items()) {
                        node.values[name] = ReadNumbers(value);
                    }
                }
                if (const auto settings = item.find("settings"); settings != item.end() && settings->is_object()) {
                    for (const auto& [name, value] : settings->items()) {
                        if (value.is_string()) {
                            node.settings[name] = value.get<std::string>();
                        }
                    }
                }
                if (const auto preview = item.find("preview"); preview != item.end() && preview->is_boolean()) {
                    node.preview = preview->get<bool>();
                }
                graph.nodes.push_back(std::move(node));
            }
        }
        if (graph.subGraph) {
            if (!graph.FindSubGraphOutput()) {
                Node output;
                output.id = graph.NextNodeId();
                output.type = std::string(kSubGraphOutputType);
                output.x = 400.0f;
                output.settings["Outputs"] = std::string(kDefaultSubGraphOutputs);
                graph.nodes.push_back(std::move(output));
                warnings.push_back("出口のノードが無かったので足しました");
            }
        } else if (!graph.FindMaster()) {
            Node master;
            master.id = graph.NextNodeId();
            master.type = std::string(kMasterNodeType);
            master.x = 400.0f;
            graph.nodes.push_back(std::move(master));
            warnings.push_back("出口のノードが無かったので足しました");
        }
        if (!graph.subGraph && Is3D(graph.target) && !graph.FindVertexMaster()) {
            Node vertex;
            vertex.id = graph.NextNodeId();
            vertex.type = std::string(kVertexMasterNodeType);
            vertex.x = 400.0f;
            vertex.y = -160.0f;
            graph.nodes.push_back(std::move(vertex));
            warnings.push_back("頂点の出口のノードが無かったので足しました");
        }

        if (const auto links = root.find("links"); links != root.end() && links->is_array()) {
            for (const json& item : *links) {
                const json from = item.value("from", json());
                const json to = item.value("to", json());
                if (!from.is_array() || from.size() != 2 || !to.is_array() || to.size() != 2
                    || !from[0].is_number_integer() || !from[1].is_string()
                    || !to[0].is_number_integer() || !to[1].is_string()) {
                    warnings.push_back("形の違うつながりを読み飛ばしました");
                    continue;
                }
                Link link{ from[0].get<int>(), from[1].get<std::string>(), to[0].get<int>(), to[1].get<std::string>() };
                if (!graph.FindNode(link.fromNode) || !graph.FindNode(link.toNode)) {
                    warnings.push_back("無いノードを指すつながりを読み飛ばしました");
                    continue;
                }
                if (graph.FindLinkTo(link.toNode, link.toPin)) {
                    warnings.push_back("同じ入力への 2 本目のつながりを読み飛ばしました");
                    continue;
                }
                graph.links.push_back(std::move(link));
            }
        }

        // グループ（無いノードと、前のグループに入ったノードは外す・中が空なら読まない）
        if (const auto groups = root.find("groups"); groups != root.end() && groups->is_array()) {
            std::set<int> grouped;
            for (const json& item : *groups) {
                if (!item.is_object()) {
                    continue;
                }
                Group group;
                if (const auto id = item.find("id"); id != item.end() && id->is_number_integer()) {
                    group.id = id->get<int>();
                }
                if (const auto title = item.find("title"); title != item.end() && title->is_string()) {
                    group.title = title->get<std::string>();
                }
                if (const auto color = item.find("color"); color != item.end() && color->is_string()) {
                    group.color = color->get<std::string>();
                }
                if (const auto members = item.find("nodes"); members != item.end() && members->is_array()) {
                    for (const json& member : *members) {
                        if (member.is_number_integer() && graph.FindNode(member.get<int>())
                            && grouped.insert(member.get<int>()).second) {
                            group.nodes.push_back(member.get<int>());
                        }
                    }
                }
                if (group.nodes.empty()) {
                    continue;
                }
                if (group.id <= 0 || graph.FindGroup(group.id)) {
                    group.id = graph.NextGroupId();
                }
                graph.groups.push_back(std::move(group));
            }
        }
        return true;
    }

    json WriteGraph(const Graph& graph)
    {
        json root = json::object();
        root["version"] = graph.version;
        root["target"] = ToString(graph.target);
        if (graph.subGraph) {
            root["subGraph"] = true;
            root["category"] = graph.category;
        }

        json properties = json::array();
        for (const Property& property : graph.properties) {
            json item = json::object();
            item["id"] = property.id;
            item["name"] = property.name;
            item["display"] = property.display;
            item["type"] = ToString(property.type);
            if (property.type == PropertyType::Texture2D) {
                item["default"] = property.texture;
                item["linear"] = property.linear;
            } else if (property.type != PropertyType::SamplerState) {
                item["default"] = WriteNumbers(property.value, WidthOf(property.type));
            }
            if (property.hasRange) {
                item["range"] = json::array({ property.rangeMin, property.rangeMax });
            }
            if (property.type == PropertyType::Color) {
                item["hdr"] = property.hdr;
            }
            properties.push_back(std::move(item));
        }
        root["properties"] = std::move(properties);

        std::vector<const Node*> nodes;
        for (const Node& node : graph.nodes) {
            nodes.push_back(&node);
        }
        std::sort(nodes.begin(), nodes.end(), [](const Node* a, const Node* b) { return a->id < b->id; });
        json nodeArray = json::array();
        for (const Node* node : nodes) {
            json item = json::object();
            item["id"] = node->id;
            item["type"] = node->type;
            item["pos"] = json::array({ std::round(node->x), std::round(node->y) });
            if (!node->property.empty()) {
                item["property"] = node->property;
            }
            if (!node->values.empty()) {
                json values = json::object();
                for (const auto& [name, numbers] : node->values) {
                    values[name] = WriteNumbers(numbers, 4);
                }
                item["values"] = std::move(values);
            }
            if (!node->settings.empty()) {
                json settings = json::object();
                for (const auto& [name, value] : node->settings) {
                    settings[name] = value;
                }
                item["settings"] = std::move(settings);
            }
            if (node->preview) {
                item["preview"] = true;
            }
            nodeArray.push_back(std::move(item));
        }
        root["nodes"] = std::move(nodeArray);

        std::vector<const Link*> links;
        for (const Link& link : graph.links) {
            links.push_back(&link);
        }
        std::sort(links.begin(), links.end(), [](const Link* a, const Link* b) {
            return std::tie(a->toNode, a->toPin, a->fromNode, a->fromPin) < std::tie(b->toNode, b->toPin, b->fromNode, b->fromPin);
        });
        json linkArray = json::array();
        for (const Link* link : links) {
            linkArray.push_back(json{ { "from", json::array({ link->fromNode, link->fromPin }) },
                                      { "to", json::array({ link->toNode, link->toPin }) } });
        }
        root["links"] = std::move(linkArray);

        if (!graph.groups.empty()) {
            std::vector<const Group*> groups;
            for (const Group& group : graph.groups) {
                groups.push_back(&group);
            }
            std::sort(groups.begin(), groups.end(), [](const Group* a, const Group* b) { return a->id < b->id; });
            json groupArray = json::array();
            for (const Group* group : groups) {
                std::vector<int> members = group->nodes;
                std::sort(members.begin(), members.end());
                groupArray.push_back(json{ { "id", group->id }, { "title", group->title }, { "color", group->color },
                                           { "nodes", members } });
            }
            root["groups"] = std::move(groupArray);
        }
        return root;
    }

    bool IsValidPropertyName(std::string_view name, std::string* error)
    {
        return IsValidIdentifier(name, error);
    }

    bool IsValidIdentifier(std::string_view name, std::string* error)
    {
        const auto fail = [error](std::string message) {
            if (error) {
                *error = std::move(message);
            }
            return false;
        };
        if (name.empty()) {
            return fail("名前が空です");
        }
        if (!std::isalpha(static_cast<unsigned char>(name[0]))) {
            return fail("名前は英字で始めてください");
        }
        for (const char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                return fail("名前には英数字と _ だけを使えます");
            }
        }
        if (std::find(std::begin(kReservedNames), std::end(kReservedNames), name) != std::end(kReservedNames)) {
            return fail(std::string(name) + " はシェーダが使う名前なので使えません");
        }
        if (name.starts_with("SG_")) {
            return fail("SG_ で始まる名前はグラフの関数が使うので使えません");
        }
        if (name.size() >= 2 && name[0] == 'n' && std::isdigit(static_cast<unsigned char>(name[1]))) {
            return fail("n と数字で始まる名前はノードの値が使うので使えません");
        }
        return true;
    }
}

#endif // CORE_EDITOR
