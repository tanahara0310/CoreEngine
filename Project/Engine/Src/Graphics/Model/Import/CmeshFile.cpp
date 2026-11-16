#include "pch.h"
#include "CmeshFile.h"
#include "Utility/Compression/CompressedFile.h"
#include "Utility/Logger/Logger.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
            static constexpr bool kReading = false;

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

            /// @brief 真偽を 4 バイトの 1 か 0 で書く
            void Bool32(bool value) { U32(value ? 1u : 0u); }

            /// @brief 列挙を 4 バイトで書く
            template<typename E>
            void Enum32(E value) { U32(static_cast<uint32_t>(value)); }

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

            /// @brief 要素を数を付けずに生のまま並べる（チャンクの残り全部になる）
            template<typename T>
            void Rest(const std::vector<T>& values)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Bytes(values.data(), values.size() * sizeof(T));
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
            static constexpr bool kReading = true;

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

            template<typename T>
            void Pod(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Bytes(&value, sizeof(T));
            }

            uint32_t U32() { return Pod<uint32_t>(); }

            /// @brief 4 バイトの数を読み、0 でなければ真にする
            void Bool32(bool& value) { value = U32() != 0; }

            /// @brief 4 バイトの数を列挙として読む
            template<typename E>
            void Enum32(E& value) { value = static_cast<E>(U32()); }

            void Str(std::string& text)
            {
                const uint32_t size = U32();
                if (size > kMaxStringLength || size > Remaining()) {
                    throw std::runtime_error("cmesh の文字列の長さが壊れています");
                }
                text.assign(size, '\0');
                Bytes(text.data(), size);
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
            void Rest(std::vector<T>& values)
            {
                if (Remaining() % sizeof(T) != 0) {
                    throw std::runtime_error("cmesh のチャンクの大きさが要素の大きさで割り切れません");
                }
                values.resize(Remaining() / sizeof(T));
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

        /// @brief 書く側（Writer）なら const T&、読む側（Reader）なら T&
        template<class Ar, class T>
        using Ref = std::conditional_t<Ar::kReading, T&, const T&>;

        /// @brief 要素の数（4 バイト）と、要素を 1 つずつ書くか読む（読むときは後ろへ足す）
        /// @param brokenMessage 読んだ数が残りのバイト数より多いときの例外の文
        /// @param element 要素 1 つを書くか読む
        template<class Ar, class Vector, class Element>
        void Records(Ar& ar, Vector& values, const char* brokenMessage, Element&& element)
        {
            if constexpr (Ar::kReading) {
                const uint32_t count = ar.U32();
                if (count > ar.Remaining()) {
                    throw std::runtime_error(brokenMessage);
                }
                values.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    element(values.emplace_back());
                }
            } else {
                ar.U32(static_cast<uint32_t>(values.size()));
                for (auto& value : values) {
                    element(value);
                }
            }
        }

        /// @brief 要素の数（4 バイト）と、名前（文字列）の付いた要素を 1 つずつ書くか読む（名前で引く表）
        /// @param brokenMessage 読んだ数が残りのバイト数より多いときの例外の文
        /// @param element 要素 1 つ（名前の後ろ）を書くか読む
        template<class Ar, class Map, class Element>
        void NamedRecords(Ar& ar, Map& values, const char* brokenMessage, Element&& element)
        {
            if constexpr (Ar::kReading) {
                const uint32_t count = ar.U32();
                if (count > ar.Remaining()) {
                    throw std::runtime_error(brokenMessage);
                }
                for (uint32_t i = 0; i < count; ++i) {
                    std::string name;
                    ar.Str(name);
                    element(values[name]);
                }
            } else {
                ar.U32(static_cast<uint32_t>(values.size()));
                for (const auto& [name, value] : values) {
                    ar.Str(name);
                    element(value);
                }
            }
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, Node> node)
        {
            ar.Str(node.name);
            ar.Pod(node.transform.scale);
            ar.Pod(node.transform.rotate);
            ar.Pod(node.transform.translate);
            ar.Pod(node.localMatrix);
            Records(ar, node.children, "cmesh のノードの数が壊れています",
                [&](auto& child) { Transfer(ar, child); });
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, MaterialAsset> material)
        {
            ar.Str(material.name);
            ar.Str(material.baseColorTexture);
            ar.Str(material.metallicRoughnessTexture);
            ar.Str(material.normalTexture);
            ar.Str(material.occlusionTexture);
            ar.Str(material.emissiveTexture);
            ar.Pod(material.baseColorFactor);
            ar.Pod(material.metallicFactor);
            ar.Pod(material.roughnessFactor);
            ar.Pod(material.emissiveFactor);
            ar.Pod(material.alphaCutoff);
            ar.Bool32(material.alphaMask);
            ar.Enum32(material.vertexAnimation);
            ar.Pod(material.vertexAnimStrength);
            ar.Pod(material.vertexAnimSpeed);
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, SubMeshData> subMesh)
        {
            ar.Str(subMesh.name);
            ar.Pod(subMesh.startIndex);
            ar.Pod(subMesh.indexCount);
            ar.Pod(subMesh.materialIndex);
            ar.Pod(subMesh.lodCount);
            for (auto& lod : subMesh.lods) {
                ar.Pod(lod);
            }
            if constexpr (Ar::kReading) {
                if (subMesh.lodCount == 0 || subMesh.lodCount > SubMeshData::kMaxLodCount) {
                    throw std::runtime_error("cmesh の LOD の段数が壊れています");
                }
            }
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, JointWeightData> joint)
        {
            ar.Pod(joint.inverseBindPoseMatrix);
            ar.Array(joint.vertexWeights);
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, NodeAnimation> nodeAnimation)
        {
            ar.Array(nodeAnimation.translate.keyframes);
            ar.Array(nodeAnimation.rotate.keyframes);
            ar.Array(nodeAnimation.scale.keyframes);
        }

        template<class Ar>
        void Transfer(Ar& ar, Ref<Ar, Animation> animation)
        {
            ar.Pod(animation.duration);
            NamedRecords(ar, animation.nodeAnimations, "cmesh のアニメーションのノードの数が壊れています",
                [&](auto& nodeAnimation) { Transfer(ar, nodeAnimation); });
        }

        /// @brief チャンク 1 つの中身を書くか読む（知らない名前は何もしない）
        /// @details 読むとき、同じ名前のチャンクが前にもあれば、VERT・VCOL・INDX・SUBM・NODE は置き換え、
        ///          MATL・ANIM は後ろへ足し、SKIN は同じ名前のジョイントを置き換える。
        template<class Ar>
        void TransferChunk(Ar& ar, std::string_view id, Ref<Ar, MeshAsset> asset)
        {
            auto& model = asset.model;
            if (id == "VERT") {
                ar.Rest(model.vertices);
            } else if (id == "VCOL") {
                ar.Rest(model.vertexColors);
            } else if (id == "INDX") {
                ar.Rest(model.indices);
            } else if (id == "SUBM") {
                if constexpr (Ar::kReading) {
                    model.subMeshes.clear();
                }
                Records(ar, model.subMeshes, "cmesh のサブメッシュの数が壊れています",
                    [&](auto& subMesh) { Transfer(ar, subMesh); });
            } else if (id == "MATL") {
                Records(ar, model.materials, "cmesh のマテリアルの数が壊れています",
                    [&](auto& material) { Transfer(ar, material); });
            } else if (id == "NODE") {
                if constexpr (Ar::kReading) {
                    model.rootNode = Node{};
                }
                Transfer(ar, model.rootNode);
            } else if (id == "SKIN") {
                NamedRecords(ar, model.skinClusterData, "cmesh のジョイントの数が壊れています",
                    [&](auto& joint) { Transfer(ar, joint); });
            } else if (id == "ANIM") {
                Records(ar, asset.animations, "cmesh のアニメーションの数が壊れています",
                    [&](auto& animation) {
                        ar.Str(animation.first);
                        Transfer(ar, animation.second);
                    });
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

        const auto writeChunk = [&](const char (&id)[5]) {
            const size_t at = writer.BeginChunk(id);
            TransferChunk(writer, std::string_view(id, 4), asset);
            writer.EndChunk(at);
        };

        const ModelData& model = asset.model;
        if (asset.hasMesh) {
            writeChunk("VERT");

            // 頂点の色は持つモデルだけ（頂点と同じ数・同じ並び）
            if (!model.vertexColors.empty() && model.vertexColors.size() == model.vertices.size()) {
                writeChunk("VCOL");
            }

            writeChunk("INDX");
            writeChunk("SUBM");
            writeChunk("MATL");
            writeChunk("NODE");

            if (!model.skinClusterData.empty()) {
                writeChunk("SKIN");
            }
        }

        if (!asset.animations.empty()) {
            writeChunk("ANIM");
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
        std::vector<uint8_t> data;
        if (!CompressedFile::Read(path, data)) {
            return false;
        }

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
                TransferChunk(chunkReader, std::string_view(chunk.id, 4), result);
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
