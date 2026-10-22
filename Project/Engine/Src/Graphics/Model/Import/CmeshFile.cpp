#include "pch.h"
#include "CmeshFile.h"
#include "Utility/Logger/Logger.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace CoreEngine
{
    namespace
    {
        constexpr char kMagic[4] = { 'C', 'M', 'S', 'H' };
        constexpr size_t kAlignment = 16;
        constexpr uint32_t kMaxStringLength = 1u << 20;

        enum Flags : uint32_t
        {
            kHasMesh = 1u << 0,
            kHasSkin = 1u << 1,
            kHasAnimation = 1u << 2,
        };

        struct Header
        {
            char magic[4];
            uint32_t version;
            uint32_t flags;
            uint32_t vertexStride;
            uint32_t lod0IndexCount;
            uint32_t chunkCount;
            uint32_t reserved[2];
        };
        static_assert(sizeof(Header) == 32);

        struct ChunkHeader
        {
            char id[4];
            uint32_t reserved;
            uint64_t size;
        };
        static_assert(sizeof(ChunkHeader) == 16);

        static_assert(std::is_trivially_copyable_v<VertexData>);
        static_assert(std::is_trivially_copyable_v<SubMeshLodRange>);
        static_assert(std::is_trivially_copyable_v<VertexWeightData>);
        static_assert(std::is_trivially_copyable_v<Matrix4x4>);
        static_assert(std::is_trivially_copyable_v<KeyframeVector3>);
        static_assert(std::is_trivially_copyable_v<KeyframeQuaternion>);

        /// @brief バイト列へ書き足す
        class Writer
        {
        public:
            void Bytes(const void* data, size_t size)
            {
                const auto* bytes = static_cast<const uint8_t*>(data);
                data_.insert(data_.end(), bytes, bytes + size);
            }

            template<typename T>
            void Pod(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Bytes(&value, sizeof(T));
            }

            void U32(uint32_t value) { Pod(value); }
            void F32(float value) { Pod(value); }

            void Str(const std::string& text)
            {
                U32(static_cast<uint32_t>(text.size()));
                Bytes(text.data(), text.size());
            }

            template<typename T>
            void Array(const std::vector<T>& values)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                U32(static_cast<uint32_t>(values.size()));
                if (!values.empty()) {
                    Bytes(values.data(), values.size() * sizeof(T));
                }
            }

            /// @brief チャンクを始める（16 バイト境界に揃えてから見出しを置く）
            size_t BeginChunk(const char (&id)[5])
            {
                while (data_.size() % kAlignment != 0) {
                    data_.push_back(0);
                }
                ChunkHeader header{};
                std::memcpy(header.id, id, 4);
                const size_t at = data_.size();
                Pod(header);
                return at;
            }

            void EndChunk(size_t at)
            {
                const uint64_t size = data_.size() - at - sizeof(ChunkHeader);
                std::memcpy(data_.data() + at + offsetof(ChunkHeader, size), &size, sizeof(size));
                ++chunkCount_;
            }

            std::vector<uint8_t>& Data() { return data_; }
            uint32_t ChunkCount() const { return chunkCount_; }

        private:
            std::vector<uint8_t> data_;
            uint32_t chunkCount_ = 0;
        };

        /// @brief バイト列から読む（足りなければ例外）
        class Reader
        {
        public:
            Reader(const uint8_t* data, size_t size) : cursor_(data), end_(data + size) {}

            void Bytes(void* out, size_t size)
            {
                if (Remaining() < size) {
                    throw std::runtime_error("cmesh の中身が途中で終わっています");
                }
                std::memcpy(out, cursor_, size);
                cursor_ += size;
            }

            template<typename T>
            T Pod()
            {
                static_assert(std::is_trivially_copyable_v<T>);
                T value;
                Bytes(&value, sizeof(T));
                return value;
            }

            uint32_t U32() { return Pod<uint32_t>(); }
            float F32() { return Pod<float>(); }

            std::string Str()
            {
                const uint32_t size = U32();
                if (size > kMaxStringLength || size > Remaining()) {
                    throw std::runtime_error("cmesh の文字列の長さが壊れています");
                }
                std::string text(size, '\0');
                Bytes(text.data(), size);
                return text;
            }

            template<typename T>
            void Array(std::vector<T>& values)
            {
                const uint32_t count = U32();
                if (count > Remaining() / sizeof(T)) {
                    throw std::runtime_error("cmesh の配列の長さが壊れています");
                }
                values.resize(count);
                if (count > 0) {
                    Bytes(values.data(), count * sizeof(T));
                }
            }

            /// @brief 残りを生のまま配列にする（要素の大きさで割り切れなければ例外）
            template<typename T>
            void Rest(std::vector<T>& values, size_t elementSize)
            {
                if (Remaining() % elementSize != 0) {
                    throw std::runtime_error("cmesh のチャンクの大きさが要素の大きさで割り切れません");
                }
                values.resize(Remaining() / elementSize);
                if (!values.empty()) {
                    Bytes(values.data(), Remaining());
                }
            }

            size_t Remaining() const { return static_cast<size_t>(end_ - cursor_); }
            const uint8_t* Cursor() const { return cursor_; }
            void Skip(size_t size)
            {
                if (Remaining() < size) {
                    throw std::runtime_error("cmesh のチャンクが途中で終わっています");
                }
                cursor_ += size;
            }

        private:
            const uint8_t* cursor_;
            const uint8_t* end_;
        };

        void WriteNode(Writer& writer, const Node& node)
        {
            writer.Str(node.name);
            writer.Pod(node.transform.scale);
            writer.Pod(node.transform.rotate);
            writer.Pod(node.transform.translate);
            writer.Pod(node.localMatrix);
            writer.U32(static_cast<uint32_t>(node.children.size()));
            for (const Node& child : node.children) {
                WriteNode(writer, child);
            }
        }

        Node ReadNode(Reader& reader)
        {
            Node node;
            node.name = reader.Str();
            node.transform.scale = reader.Pod<Vector3>();
            node.transform.rotate = reader.Pod<Quaternion>();
            node.transform.translate = reader.Pod<Vector3>();
            node.localMatrix = reader.Pod<Matrix4x4>();
            const uint32_t childCount = reader.U32();
            if (childCount > reader.Remaining()) {
                throw std::runtime_error("cmesh のノードの数が壊れています");
            }
            node.children.reserve(childCount);
            for (uint32_t i = 0; i < childCount; ++i) {
                node.children.push_back(ReadNode(reader));
            }
            return node;
        }

        void WriteMaterial(Writer& writer, const MaterialAsset& material)
        {
            writer.Str(material.name);
            writer.Str(material.baseColorTexture);
            writer.Str(material.metallicRoughnessTexture);
            writer.Str(material.normalTexture);
            writer.Str(material.occlusionTexture);
            writer.Str(material.emissiveTexture);
            writer.Pod(material.baseColorFactor);
            writer.F32(material.metallicFactor);
            writer.F32(material.roughnessFactor);
            writer.Pod(material.emissiveFactor);
            writer.F32(material.alphaCutoff);
            writer.U32(material.alphaMask ? 1u : 0u);
            writer.U32(static_cast<uint32_t>(material.vertexAnimation));
            writer.F32(material.vertexAnimStrength);
            writer.F32(material.vertexAnimSpeed);
        }

        MaterialAsset ReadMaterial(Reader& reader)
        {
            MaterialAsset material;
            material.name = reader.Str();
            material.baseColorTexture = reader.Str();
            material.metallicRoughnessTexture = reader.Str();
            material.normalTexture = reader.Str();
            material.occlusionTexture = reader.Str();
            material.emissiveTexture = reader.Str();
            material.baseColorFactor = reader.Pod<Vector4>();
            material.metallicFactor = reader.F32();
            material.roughnessFactor = reader.F32();
            material.emissiveFactor = reader.Pod<Vector3>();
            material.alphaCutoff = reader.F32();
            material.alphaMask = reader.U32() != 0;
            material.vertexAnimation = static_cast<VertexAnimationType>(reader.U32());
            material.vertexAnimStrength = reader.F32();
            material.vertexAnimSpeed = reader.F32();
            return material;
        }

        void WriteAnimation(Writer& writer, const std::string& name, const Animation& animation)
        {
            writer.Str(name);
            writer.F32(animation.duration);
            writer.U32(static_cast<uint32_t>(animation.nodeAnimations.size()));
            for (const auto& [nodeName, nodeAnimation] : animation.nodeAnimations) {
                writer.Str(nodeName);
                writer.Array(nodeAnimation.translate.keyframes);
                writer.Array(nodeAnimation.rotate.keyframes);
                writer.Array(nodeAnimation.scale.keyframes);
            }
        }

        std::pair<std::string, Animation> ReadAnimation(Reader& reader)
        {
            std::pair<std::string, Animation> result;
            result.first = reader.Str();
            Animation& animation = result.second;
            animation.duration = reader.F32();
            const uint32_t nodeCount = reader.U32();
            if (nodeCount > reader.Remaining()) {
                throw std::runtime_error("cmesh のアニメーションのノードの数が壊れています");
            }
            for (uint32_t i = 0; i < nodeCount; ++i) {
                const std::string nodeName = reader.Str();
                NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeName];
                reader.Array(nodeAnimation.translate.keyframes);
                reader.Array(nodeAnimation.rotate.keyframes);
                reader.Array(nodeAnimation.scale.keyframes);
            }
            return result;
        }

        /// @brief チャンク 1 つを読む（知らない名前は何もしない）
        void ReadChunk(const ChunkHeader& header, Reader& reader, MeshAsset& asset)
        {
            ModelData& model = asset.model;
            const std::string_view id(header.id, 4);
            if (id == "VERT") {
                reader.Rest(model.vertices, sizeof(VertexData));
            } else if (id == "INDX") {
                reader.Rest(model.indices, sizeof(int32_t));
            } else if (id == "SUBM") {
                const uint32_t count = reader.U32();
                if (count > reader.Remaining()) {
                    throw std::runtime_error("cmesh のサブメッシュの数が壊れています");
                }
                model.subMeshes.resize(count);
                for (SubMeshData& subMesh : model.subMeshes) {
                    subMesh.name = reader.Str();
                    subMesh.startIndex = reader.U32();
                    subMesh.indexCount = reader.U32();
                    subMesh.materialIndex = reader.U32();
                    subMesh.lodCount = reader.U32();
                    for (SubMeshLodRange& lod : subMesh.lods) {
                        lod = reader.Pod<SubMeshLodRange>();
                    }
                    if (subMesh.lodCount == 0 || subMesh.lodCount > SubMeshData::kMaxLodCount) {
                        throw std::runtime_error("cmesh の LOD の段数が壊れています");
                    }
                }
            } else if (id == "MATL") {
                const uint32_t count = reader.U32();
                if (count > reader.Remaining()) {
                    throw std::runtime_error("cmesh のマテリアルの数が壊れています");
                }
                model.materials.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    model.materials.push_back(ReadMaterial(reader));
                }
            } else if (id == "NODE") {
                model.rootNode = ReadNode(reader);
            } else if (id == "SKIN") {
                const uint32_t count = reader.U32();
                if (count > reader.Remaining()) {
                    throw std::runtime_error("cmesh のジョイントの数が壊れています");
                }
                for (uint32_t i = 0; i < count; ++i) {
                    const std::string jointName = reader.Str();
                    JointWeightData& joint = model.skinClusterData[jointName];
                    joint.inverseBindPoseMatrix = reader.Pod<Matrix4x4>();
                    reader.Array(joint.vertexWeights);
                }
            } else if (id == "ANIM") {
                const uint32_t count = reader.U32();
                if (count > reader.Remaining()) {
                    throw std::runtime_error("cmesh のアニメーションの数が壊れています");
                }
                asset.animations.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    asset.animations.push_back(ReadAnimation(reader));
                }
            }
        }
    }

    bool CmeshFile::Write(const std::filesystem::path& path, const MeshAsset& asset)
    {
        Writer writer;
        Header header{};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.version = kVersion;
        header.flags = (asset.hasMesh ? kHasMesh : 0u)
            | (!asset.model.skinClusterData.empty() ? kHasSkin : 0u)
            | (!asset.animations.empty() ? kHasAnimation : 0u);
        header.vertexStride = sizeof(VertexData);
        header.lod0IndexCount = asset.lod0IndexCount;
        writer.Pod(header);

        const ModelData& model = asset.model;
        if (asset.hasMesh) {
            size_t at = writer.BeginChunk("VERT");
            writer.Bytes(model.vertices.data(), model.vertices.size() * sizeof(VertexData));
            writer.EndChunk(at);

            at = writer.BeginChunk("INDX");
            writer.Bytes(model.indices.data(), model.indices.size() * sizeof(int32_t));
            writer.EndChunk(at);

            at = writer.BeginChunk("SUBM");
            writer.U32(static_cast<uint32_t>(model.subMeshes.size()));
            for (const SubMeshData& subMesh : model.subMeshes) {
                writer.Str(subMesh.name);
                writer.U32(subMesh.startIndex);
                writer.U32(subMesh.indexCount);
                writer.U32(subMesh.materialIndex);
                writer.U32(subMesh.lodCount);
                for (const SubMeshLodRange& lod : subMesh.lods) {
                    writer.Pod(lod);
                }
            }
            writer.EndChunk(at);

            at = writer.BeginChunk("MATL");
            writer.U32(static_cast<uint32_t>(model.materials.size()));
            for (const MaterialAsset& material : model.materials) {
                WriteMaterial(writer, material);
            }
            writer.EndChunk(at);

            at = writer.BeginChunk("NODE");
            WriteNode(writer, model.rootNode);
            writer.EndChunk(at);

            if (!model.skinClusterData.empty()) {
                at = writer.BeginChunk("SKIN");
                writer.U32(static_cast<uint32_t>(model.skinClusterData.size()));
                for (const auto& [jointName, joint] : model.skinClusterData) {
                    writer.Str(jointName);
                    writer.Pod(joint.inverseBindPoseMatrix);
                    writer.Array(joint.vertexWeights);
                }
                writer.EndChunk(at);
            }
        }

        if (!asset.animations.empty()) {
            const size_t at = writer.BeginChunk("ANIM");
            writer.U32(static_cast<uint32_t>(asset.animations.size()));
            for (const auto& [name, animation] : asset.animations) {
                WriteAnimation(writer, name, animation);
            }
            writer.EndChunk(at);
        }

        std::vector<uint8_t>& data = writer.Data();
        const uint32_t chunkCount = writer.ChunkCount();
        std::memcpy(data.data() + offsetof(Header, chunkCount), &chunkCount, sizeof(chunkCount));

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(file);
    }

    bool CmeshFile::Read(const std::filesystem::path& path, MeshAsset& asset)
    {
        Logger& log = Logger::GetInstance();
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        try {
            Reader reader(data.data(), data.size());
            const Header header = reader.Pod<Header>();
            if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "cmesh の版が違うので読めません: {}", log.PathToUtf8(path));
                return false;
            }
            const bool hasMesh = (header.flags & kHasMesh) != 0;
            if (hasMesh && header.vertexStride != sizeof(VertexData)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "cmesh の頂点の大きさが違うので読めません: {}", log.PathToUtf8(path));
                return false;
            }

            MeshAsset result;
            result.hasMesh = hasMesh;
            result.lod0IndexCount = header.lod0IndexCount;
            for (uint32_t i = 0; i < header.chunkCount; ++i) {
                const size_t offset = static_cast<size_t>(reader.Cursor() - data.data());
                reader.Skip((kAlignment - offset % kAlignment) % kAlignment);
                const ChunkHeader chunk = reader.Pod<ChunkHeader>();
                if (chunk.size > reader.Remaining()) {
                    throw std::runtime_error("cmesh のチャンクの大きさが壊れています");
                }
                Reader chunkReader(reader.Cursor(), static_cast<size_t>(chunk.size));
                ReadChunk(chunk, chunkReader, result);
                reader.Skip(static_cast<size_t>(chunk.size));
            }

            if (hasMesh && (result.model.vertices.empty() || result.model.indices.empty() ||
                result.lod0IndexCount > result.model.indices.size())) {
                throw std::runtime_error("cmesh のメッシュが欠けています");
            }
            asset = std::move(result);
            return true;
        }
        catch (const std::exception& e) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "cmesh を読めません: {}（{}）", log.PathToUtf8(path), e.what());
            return false;
        }
    }
}
