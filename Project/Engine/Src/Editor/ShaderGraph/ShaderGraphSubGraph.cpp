#include "pch.h"
#include "ShaderGraphSubGraph.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphCodeGen.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <tuple>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief 出口から入力をたどって届くノード（出口も含む）
        std::vector<int> Reachable(const Graph& graph, const Node& root)
        {
            std::vector<int> order;
            std::set<int> visited;
            std::function<void(int)> visit = [&](int id) {
                if (!visited.insert(id).second) {
                    return;
                }
                const Node* const node = graph.FindNode(id);
                const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
                if (desc) {
                    for (const PinDesc& pin : desc->inputs) {
                        if (const Link* const link = graph.FindLinkTo(id, pin.name)) {
                            visit(link->fromNode);
                        }
                    }
                }
                order.push_back(id);
            };
            visit(root.id);
            return order;
        }

        /// @brief 設定の値（書いていなければ空）
        std::string SettingOf(const Node& node, std::string_view name)
        {
            const auto found = node.settings.find(name);
            return found != node.settings.end() ? found->second : std::string();
        }

        /// @brief サブグラフの指す先の名前（GUID か、無ければパス）
        std::string IdentityOf(const std::string& guid, const std::string& path)
        {
            return guid.empty() ? path : guid;
        }

        /// @brief 使える目標を、出口からたどったノードの重なりで求める（入れ子のサブグラフの中もたどる）
        void CollectTargets(const Graph& subGraph, const SubGraphResolver& resolve, std::vector<std::string>& chain,
            uint8_t& targets)
        {
            const Node* const output = subGraph.FindSubGraphOutput();
            if (!output) {
                return;
            }
            for (const int id : Reachable(subGraph, *output)) {
                const Node* const node = subGraph.FindNode(id);
                if (!node || node->id == output->id) {
                    continue;
                }
                const NodeDesc* const desc = FindNodeDesc(*node);
                if (!desc) {
                    continue;
                }
                if (node->type == kSubGraphNodeType && resolve) {
                    const std::string guid = SettingOf(*node, "Guid");
                    const std::string path = SettingOf(*node, "Path");
                    const std::string identity = IdentityOf(guid, path);
                    std::string error;
                    const std::shared_ptr<const Graph> inner =
                        std::find(chain.begin(), chain.end(), identity) == chain.end() ? resolve(guid, path, error) : nullptr;
                    if (inner) {
                        chain.push_back(identity);
                        CollectTargets(*inner, resolve, chain, targets);
                        chain.pop_back();
                        continue;
                    }
                }
                targets &= desc->targets;
            }
        }

        /// @brief 文字から、シェーダの名前に使える部分（英数字）だけを残し、頭を小文字にする
        std::string ToIdentifier(std::string_view text)
        {
            std::string result;
            for (const char c : text) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    result += c;
                }
            }
            if (!result.empty()) {
                result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
            }
            return result;
        }

        /// @brief ピンの種類からプロパティの種類（数は幅で決める）
        PropertyType ToPropertyType(ValueType type)
        {
            switch (type) {
            case ValueType::Texture2D: return PropertyType::Texture2D;
            case ValueType::SamplerState: return PropertyType::SamplerState;
            default: break;
            }
            switch (WidthOf(type)) {
            case 2: return PropertyType::Vector2;
            case 3: return PropertyType::Vector3;
            case 4: return PropertyType::Vector4;
            default: return PropertyType::Float;
            }
        }

        /// @brief 出力ピンの決まった種類
        ValueType OutputType(const Graph& graph, const Resolution& resolution, int node, std::string_view pin)
        {
            const Node* const source = graph.FindNode(node);
            const NodeDesc* const desc = source ? FindNodeDesc(*source) : nullptr;
            const int index = desc ? desc->FindOutput(pin) : -1;
            const auto resolved = resolution.nodes.find(node);
            if (index < 0 || resolved == resolution.nodes.end()
                || static_cast<size_t>(index) >= resolved->second.outputs.size()) {
                return ValueType::Float;
            }
            return resolved->second.outputs[static_cast<size_t>(index)];
        }

        /// @brief 出力ピンの表示名
        std::string OutputLabel(const Graph& graph, int node, std::string_view pin)
        {
            const Node* const source = graph.FindNode(node);
            const NodeDesc* const desc = source ? FindNodeDesc(*source) : nullptr;
            const int index = desc ? desc->FindOutput(pin) : -1;
            return index >= 0 ? std::string(desc->outputs[static_cast<size_t>(index)].Label()) : std::string(pin);
        }

        /// @brief ノードの見出し
        std::string NodeTitle(const Graph& graph, int node)
        {
            const Node* const source = graph.FindNode(node);
            const NodeDesc* const desc = source ? FindNodeDesc(*source) : nullptr;
            return desc ? std::string(desc->title) : std::string();
        }
    }

    SubGraphSnapshot MakeSubGraphSnapshot(const Graph& subGraph, const SubGraphResolver& resolve)
    {
        SubGraphSnapshot snapshot;
        std::vector<SubGraphPin> inputs;
        for (const Property& property : subGraph.properties) {
            inputs.push_back(SubGraphPin{ property.type, property.id, property.display.empty() ? property.name : property.display });
        }
        snapshot.inputs = FormatSubGraphPins(inputs);
        if (const Node* const output = subGraph.FindSubGraphOutput()) {
            snapshot.outputs = FormatSubGraphPins(ParseSubGraphPins(SettingOf(*output, "Outputs")));
        }
        std::vector<std::string> chain;
        CollectTargets(subGraph, resolve, chain, snapshot.targets);
        return snapshot;
    }

    Node MakeSubGraphNode(int id, const std::string& guid, const std::string& path, const Graph& subGraph,
        const SubGraphSnapshot& snapshot)
    {
        Node node;
        node.id = id;
        node.type = std::string(kSubGraphNodeType);
        node.settings["Guid"] = guid;
        node.settings["Path"] = path;
        node.settings["Inputs"] = snapshot.inputs;
        node.settings["Outputs"] = snapshot.outputs;
        node.settings["Targets"] = FormatTargets(snapshot.targets);
        for (const Property& property : subGraph.properties) {
            if (IsNumeric(ToValueType(property.type))) {
                node.values[property.id] = property.value;
            }
        }
        return node;
    }

    bool ApplySubGraphSnapshot(Graph& graph, int id, const Graph& subGraph, const SubGraphSnapshot& snapshot)
    {
        Node* const node = graph.FindNode(id);
        if (!node || node->type != kSubGraphNodeType) {
            return false;
        }
        bool changed = false;
        const auto set = [&](const char* name, const std::string& value) {
            std::string& current = node->settings[name];
            if (current != value) {
                current = value;
                changed = true;
            }
        };
        set("Inputs", snapshot.inputs);
        set("Outputs", snapshot.outputs);
        set("Targets", FormatTargets(snapshot.targets));

        std::set<std::string, std::less<>> inputs;
        std::set<std::string, std::less<>> outputs;
        for (const SubGraphPin& pin : ParseSubGraphPins(snapshot.inputs)) {
            inputs.insert(pin.key);
        }
        for (const SubGraphPin& pin : ParseSubGraphPins(snapshot.outputs)) {
            outputs.insert(pin.key);
        }
        const size_t links = graph.links.size();
        std::erase_if(graph.links, [&](const Link& link) {
            return (link.toNode == id && !inputs.contains(link.toPin)) || (link.fromNode == id && !outputs.contains(link.fromPin));
        });
        changed = changed || graph.links.size() != links;

        // ノードの値: 消えた入力の値は外し、新しい数の入力にはサブグラフの既定値を入れる
        const size_t values = node->values.size();
        std::erase_if(node->values, [&](const auto& item) { return !inputs.contains(item.first); });
        changed = changed || node->values.size() != values;
        for (const Property& property : subGraph.properties) {
            if (IsNumeric(ToValueType(property.type)) && !node->values.contains(property.id)) {
                node->values[property.id] = property.value;
                changed = true;
            }
        }
        return changed;
    }

    bool SyncSubGraphNodes(Graph& graph, const SubGraphResolver& resolve)
    {
        if (!resolve) {
            return false;
        }
        bool changed = false;
        std::vector<int> ids;
        for (const Node& node : graph.nodes) {
            if (node.type == kSubGraphNodeType) {
                ids.push_back(node.id);
            }
        }
        for (const int id : ids) {
            const Node* const node = graph.FindNode(id);
            std::string error;
            const std::shared_ptr<const Graph> subGraph = resolve(SettingOf(*node, "Guid"), SettingOf(*node, "Path"), error);
            if (subGraph) {
                changed = ApplySubGraphSnapshot(graph, id, *subGraph, MakeSubGraphSnapshot(*subGraph, resolve)) || changed;
            }
        }
        return changed;
    }

    Graph MakeSubGraphPreviewGraph(const Graph& subGraph)
    {
        Graph graph = subGraph;
        graph.subGraph = false;
        graph.category.clear();

        // サンプラーの入力は、線形・繰り返しの Sampler State ノードにする
        for (Node& node : graph.nodes) {
            if (node.type != "Property") {
                continue;
            }
            const Property* const property = subGraph.FindProperty(node.property);
            if (property && property->type == PropertyType::SamplerState) {
                node.type = "SamplerState";
                node.property.clear();
                node.settings = { { "Filter", "Linear Repeat" } };
            }
        }
        std::erase_if(graph.properties, [](const Property& property) { return property.type == PropertyType::SamplerState; });

        // 出口（サブグラフの出口の最初の数の出力を Base Color へ）
        Node master;
        master.id = graph.NextNodeId();
        master.type = std::string(kMasterNodeType);
        const int masterId = master.id;
        graph.nodes.push_back(std::move(master));
        if (Is3D(graph.target)) {
            Node vertex;
            vertex.id = masterId + 1;
            vertex.type = std::string(kVertexMasterNodeType);
            graph.nodes.push_back(std::move(vertex));
        }
        if (const Node* const output = subGraph.FindSubGraphOutput()) {
            for (const SubGraphPin& pin : ParseSubGraphPins(SettingOf(*output, "Outputs"))) {
                if (!IsNumeric(ToValueType(pin.type))) {
                    continue;
                }
                if (const Link* const link = subGraph.FindLinkTo(output->id, pin.key)) {
                    graph.links.push_back(Link{ link->fromNode, link->fromPin, masterId, "Base Color" });
                }
                break;
            }
        }
        return graph;
    }

    bool UsesSubGraph(const Graph& graph, const std::string& guid, const std::string& path, const SubGraphResolver& resolve)
    {
        std::set<std::string> visited;
        std::function<bool(const Graph&)> uses = [&](const Graph& current) {
            for (const Node& node : current.nodes) {
                if (node.type != kSubGraphNodeType) {
                    continue;
                }
                const std::string nodeGuid = SettingOf(node, "Guid");
                const std::string nodePath = SettingOf(node, "Path");
                if ((!guid.empty() && nodeGuid == guid) || (!path.empty() && nodePath == path)) {
                    return true;
                }
                if (!resolve || !visited.insert(IdentityOf(nodeGuid, nodePath)).second) {
                    continue;
                }
                std::string error;
                if (const std::shared_ptr<const Graph> inner = resolve(nodeGuid, nodePath, error); inner && uses(*inner)) {
                    return true;
                }
            }
            return false;
        };
        return uses(graph);
    }

    int ExtractSubGraph(Graph& graph, const std::set<int>& selection, const std::string& guid, const std::string& path,
        const SubGraphResolver& resolve, Graph& subGraph, std::string& error)
    {
        // 中へ移すノード（出口と Property は外に残す。付箋は線を持たないので別に移す）
        std::set<int> moving;
        std::set<int> notes;
        for (const int id : selection) {
            const Node* const node = graph.FindNode(id);
            if (!node || IsMasterNodeType(node->type) || node->type == "Property") {
                continue;
            }
            (node->type == kStickyNoteType ? notes : moving).insert(id);
        }
        if (moving.empty()) {
            error = "サブグラフにするノードを選んでください（出口と Property は入りません）";
            return 0;
        }
        const Resolution resolution = Resolve(graph);

        // 外から入る線の元・外へ出る線の元（ノードの番号とピンの順）
        std::vector<std::pair<int, std::string>> inputSources;
        std::vector<std::pair<int, std::string>> outputSources;
        for (const Link& link : graph.links) {
            const bool from = moving.contains(link.fromNode);
            const bool to = moving.contains(link.toNode);
            if (!from && to) {
                inputSources.emplace_back(link.fromNode, link.fromPin);
            } else if (from && !to) {
                outputSources.emplace_back(link.fromNode, link.fromPin);
            }
        }
        for (auto* sources : { &inputSources, &outputSources }) {
            std::sort(sources->begin(), sources->end());
            sources->erase(std::unique(sources->begin(), sources->end()), sources->end());
        }
        if (inputSources.size() > kMaxSubGraphPins || outputSources.size() > kMaxSubGraphPins) {
            error = "入力か出力が " + std::to_string(kMaxSubGraphPins) + " 個を超えるので、サブグラフにできません";
            return 0;
        }

        // 中のノード（番号はそのまま。左上を原点へ寄せる）
        subGraph = Graph{};
        subGraph.target = graph.target;
        subGraph.subGraph = true;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float sumX = 0.0f;
        float sumY = 0.0f;
        bool first = true;
        for (const int id : moving) {
            const Node* const node = graph.FindNode(id);
            minX = first ? node->x : (std::min)(minX, node->x);
            minY = first ? node->y : (std::min)(minY, node->y);
            maxX = first ? node->x : (std::max)(maxX, node->x);
            sumX += node->x;
            sumY += node->y;
            first = false;
        }
        for (const std::set<int>* ids : { &moving, &notes }) {
            for (const int id : *ids) {
                Node node = *graph.FindNode(id);
                node.x -= minX;
                node.y -= minY;
                subGraph.nodes.push_back(std::move(node));
            }
        }
        for (const Link& link : graph.links) {
            if (moving.contains(link.fromNode) && moving.contains(link.toNode)) {
                subGraph.links.push_back(link);
            }
        }
        int nextId = graph.NextNodeId();
        const int newId = nextId++;
        const int outputId = nextId++;

        // 入力（外の線の元ごとに Property を 1 つ。元が Property なら、その名前・既定値を写す）
        std::set<std::string> names;
        std::vector<Link> outerLinks;
        for (size_t index = 0; index < inputSources.size(); ++index) {
            const auto& [sourceNode, sourcePin] = inputSources[index];
            Property property;
            property.id = "p" + std::to_string(index + 1);
            property.type = ToPropertyType(OutputType(graph, resolution, sourceNode, sourcePin));
            const Node* const source = graph.FindNode(sourceNode);
            const Property* const original = source && source->type == "Property" ? graph.FindProperty(source->property) : nullptr;
            if (original) {
                const PropertyType type = property.type;
                const std::string id = property.id;
                property = *original;
                property.id = id;
                if (ToValueType(property.type) != ToValueType(type)) {
                    property.type = type;
                }
            } else {
                const std::string label = OutputLabel(graph, sourceNode, sourcePin);
                const std::string title = NodeTitle(graph, sourceNode);
                property.display = label == "Out" || title.empty() ? (title.empty() ? label : title) : title + " " + label;
                property.name = ToIdentifier(property.display);
            }
            if (!IsValidPropertyName(property.name, nullptr) || names.contains(property.name)) {
                const std::string base = IsValidPropertyName(property.name, nullptr) ? property.name : std::string("input");
                for (int suffix = 1;; ++suffix) {
                    property.name = base + std::to_string(suffix);
                    if (IsValidPropertyName(property.name, nullptr) && !names.contains(property.name)) {
                        break;
                    }
                }
            }
            names.insert(property.name);

            Node value;
            value.id = nextId++;
            value.type = "Property";
            value.property = property.id;
            value.x = -320.0f;
            value.y = static_cast<float>(index) * 120.0f;
            for (const Link& link : graph.links) {
                if (link.fromNode == sourceNode && link.fromPin == sourcePin && moving.contains(link.toNode)) {
                    subGraph.links.push_back(Link{ value.id, "Out", link.toNode, link.toPin });
                }
            }
            subGraph.nodes.push_back(std::move(value));
            subGraph.properties.push_back(std::move(property));
            outerLinks.push_back(Link{ sourceNode, sourcePin, newId, "p" + std::to_string(index + 1) });
        }

        // 出力（外へ出る線の元ごとに 1 つ）
        std::vector<SubGraphPin> outputs;
        std::set<std::string> labels;
        for (size_t index = 0; index < outputSources.size(); ++index) {
            const auto& [sourceNode, sourcePin] = outputSources[index];
            SubGraphPin pin;
            pin.key = "o" + std::to_string(index + 1);
            pin.type = ToPropertyType(OutputType(graph, resolution, sourceNode, sourcePin));
            pin.label = OutputLabel(graph, sourceNode, sourcePin);
            if (!labels.insert(pin.label).second) {
                pin.label += " " + std::to_string(index + 1);
                labels.insert(pin.label);
            }
            subGraph.links.push_back(Link{ sourceNode, sourcePin, outputId, pin.key });
            for (const Link& link : graph.links) {
                if (link.fromNode == sourceNode && link.fromPin == sourcePin && !moving.contains(link.toNode)) {
                    outerLinks.push_back(Link{ newId, pin.key, link.toNode, link.toPin });
                }
            }
            outputs.push_back(std::move(pin));
        }
        Node output;
        output.id = outputId;
        output.type = std::string(kSubGraphOutputType);
        output.x = maxX - minX + 320.0f;
        output.y = 0.0f;
        output.settings["Outputs"] = outputs.empty() ? std::string(kDefaultSubGraphOutputs) : FormatSubGraphPins(outputs);
        subGraph.nodes.push_back(std::move(output));

        // 選んだノードが全部同じグループなら、新しいノードもそのグループへ
        std::optional<Group> group;
        if (const Group* const firstGroup = graph.FindGroupOf(*moving.begin())) {
            const bool same = std::all_of(moving.begin(), moving.end(),
                [&](int id) { return graph.FindGroupOf(id) == firstGroup; });
            if (same) {
                group = Group{ firstGroup->id, firstGroup->title, firstGroup->color, {} };
            }
        }

        // 使う側: 選んだノードを外し、サブグラフのノードを置いてつなぎ直す
        Node node = MakeSubGraphNode(newId, guid, path, subGraph, MakeSubGraphSnapshot(subGraph, resolve));
        const float count = static_cast<float>(moving.size());
        node.x = sumX / count;
        node.y = sumY / count;
        for (const std::set<int>* ids : { &moving, &notes }) {
            for (const int id : *ids) {
                graph.RemoveNode(id);
            }
        }
        graph.nodes.push_back(std::move(node));
        for (Link& link : outerLinks) {
            graph.links.push_back(std::move(link));
        }
        if (group) {
            if (Group* const existing = graph.FindGroup(group->id)) {
                existing->nodes.push_back(newId);
            } else {
                group->nodes.push_back(newId);
                graph.groups.push_back(std::move(*group));
            }
        }
        return newId;
    }
}

#endif // CORE_EDITOR
