#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tracy
{
    class Worker;
    struct ThreadData;
    struct ZoneEvent;
}

/// @file UpdateTree.h
/// @brief メインスレッドの Update の区間を、名前の並び（Update/PostLogic/EnvironmentFeature など）ごとにフレームをまたいで足す

namespace CoreEngine::Profiler
{
    class FrameSnapshot;
    class SourceInfoCache;
    struct ViewState;

    /// @brief フレームの見分け
    enum class FrameClass : uint8_t
    {
        None,      ///< Update の区間が無い
        Stopped,   ///< オブジェクトが進まなかった（GameObject Update の区間が無い）
        Play,      ///< オブジェクトが進んだ
        FirstPlay, ///< 止まっていた状態から進んだ最初のフレーム
        Count,
    };

    /// @brief 木の節の見分け（色に使う）
    enum class UpdateNodeGroup : uint8_t
    {
        Root,    ///< Update
        Phase,   ///< FrameStart などの段
        Feature, ///< 「名前Feature」
        Object,  ///< GameObject Update など、オブジェクトの更新
        Script,  ///< スクリプトの関数
        Other,
    };

    /// @brief 節 1 つの、フレームの見分け 1 つ分の数字（時間はナノ秒）
    struct UpdateNodeStats
    {
        int64_t total = 0;       ///< 区間の時間の合計
        int64_t childTotal = 0;  ///< 子の区間の時間の合計
        uint64_t calls = 0;
        uint32_t frames = 0;     ///< 出てきたフレームの数
        uint32_t spikes = 0;     ///< 1 回で 1 ms 以上かかった回
        uint32_t selfSpikes = 0; ///< 自分だけの時間が 1 ms 以上だったフレーム
        int64_t max = 0;         ///< いちばん長い 1 回
        const tracy::ZoneEvent* maxZone = nullptr;
        size_t maxFrame = 0;
        int64_t maxSelf = 0;     ///< 自分だけの時間がいちばん長かったフレームの値
        const tracy::ZoneEvent* maxSelfZone = nullptr; ///< そのフレームのこの区間（いちばん長い 1 回）
        size_t maxSelfFrame = 0;
    };

    struct UpdateNode
    {
        std::string name;
        std::string description;
        std::string selfDescription;
        uint32_t parent = 0;            ///< 根は自分（0）
        uint32_t depth = 0;
        UpdateNodeGroup group = UpdateNodeGroup::Other;
        bool playMarker = false;        ///< この区間があるフレームを再生中とみなす（GameObject Update）
        std::vector<uint32_t> children; ///< 動く順（親の始まりから始まるまでの時間の平均の順）
        std::array<UpdateNodeStats, static_cast<size_t>(FrameClass::Count)> stats;
        double offsetSum = 0.0;         ///< 親の始まりからこの区間が始まるまでの時間の和
        uint32_t offsetCount = 0;
    };

    /// @brief 数えるフレームの見分けを合わせた、節 1 つの数字（時間はナノ秒）
    struct UpdateNodeSummary
    {
        bool present = false;   ///< 再生の最初を含めた範囲に出てきたか
        bool firstOnly = false; ///< 再生の最初のフレームにだけ出てきた
        double mean = 0.0;      ///< 1 フレームあたり（平均に数えるフレームの数で割る）
        double selfMean = 0.0;
        double callsPerFrame = 0.0;
        uint64_t allCalls = 0;  ///< 再生の最初を含めた回数
        int64_t max = 0;
        const tracy::ZoneEvent* maxZone = nullptr;
        size_t maxFrame = 0;
        int64_t maxSelf = 0;
        const tracy::ZoneEvent* maxSelfZone = nullptr;
        size_t maxSelfFrame = 0;
        uint32_t spikes = 0;
        uint32_t selfSpikes = 0;
    };

    class UpdateTree
    {
    public:
        static constexpr uint32_t kNoNode = UINT32_MAX;
        static constexpr uint32_t kRoot = 0;

        /// @brief 数えた中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から、まだ数えていない終わったフレームを数え、選んだフレームの列と推移を写す
        void Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ViewState& view);

        bool HasRoot() const { return !nodes_.empty(); }
        const std::vector<UpdateNode>& Nodes() const { return nodes_; }
        const UpdateNode& Node(uint32_t index) const { return nodes_[index]; }

        /// @brief 平均に数えるフレームの範囲で、節の数字をまとめる（最大には再生の最初も入れる）
        UpdateNodeSummary Summarize(uint32_t node, bool includeStopped) const;

        /// @brief 再生中のフレームがあるか（無ければ停止中のフレームを平均に数える）
        bool HasPlayFrames() const;
        /// @brief 平均に数えるフレームの数
        uint64_t CountedFrames(bool includeStopped) const;
        /// @brief 平均に数える見分けか
        bool IsCounted(FrameClass frameClass, bool includeStopped) const;
        uint64_t FrameCount(FrameClass frameClass) const { return frameCount_[static_cast<size_t>(frameClass)]; }

        /// @brief 数えたフレームの数と、数えられる（終わった）フレームの数
        size_t ProcessedFrames() const { return frameClass_.size(); }
        size_t AvailableFrames() const { return available_; }

        /// @brief フレームの見分け（まだ数えていなければ None）
        FrameClass ClassOf(size_t frame) const;

        /// @brief 再生の最初のフレーム（記録の順）
        const std::vector<size_t>& FirstPlayFrames() const { return firstPlayFrames_; }
        /// @brief そのフレームの Update の時間（Update の区間が無ければ -1）
        int64_t RootTime(size_t frame) const;
        /// @brief そのフレームの Update の区間（無ければ nullptr。中身は鍵をかけている間だけ読む）
        const tracy::ZoneEvent* RootZone(size_t frame) const { return frame < frameRoot_.size() ? frameRoot_[frame] : nullptr; }

        uint64_t MainThreadId() const { return mainThreadId_; }

        /// @brief 選んだフレームを写したか（列「フレーム N」）
        bool HasSelectedFrame() const { return selectedValid_; }
        size_t SelectedFrame() const { return selectedFrame_; }
        /// @brief 選んだフレームの、節の時間と自分だけの時間
        int64_t SelectedTime(uint32_t node) const;
        int64_t SelectedSelf(uint32_t node) const;

        /// @brief 推移を写す行を決める（次の Update で写す）。kNoNode なら写さない
        void RequestTrend(uint32_t node, bool self);
        /// @brief 推移の最初のフレームと、フレームごとの時間（Update の区間が無いか、まだ数えていないフレームは -1）
        size_t TrendFirst() const { return trendFirst_; }
        const std::vector<int64_t>& TrendValues() const { return trendValues_; }
        uint32_t TrendNode() const { return trendNode_; }
        bool TrendSelf() const { return trendSelf_; }

    private:
        /// @brief 1 フレームの中で、節ごとに足す途中の値
        struct Scratch
        {
            int64_t time = 0;
            int64_t child = 0;
            uint32_t calls = 0;
            uint32_t spikes = 0;
            int64_t max = 0;
            const tracy::ZoneEvent* maxZone = nullptr;
            int64_t firstOffset = 0;
            bool touched = false;
        };

        const tracy::ZoneEvent* FindRoot(const tracy::Worker& worker, SourceInfoCache& sources, const tracy::ThreadData& thread,
            int64_t begin, int64_t end);
        uint32_t AddNode(const tracy::Worker& worker, SourceInfoCache& sources, uint32_t parent, int16_t srcloc);
        uint32_t ChildOf(const tracy::Worker& worker, SourceInfoCache& sources, uint32_t parent, int16_t srcloc);
        uint32_t FindChild(uint32_t parent, int16_t srcloc) const;
        void ProcessFrame(const tracy::Worker& worker, SourceInfoCache& sources, const tracy::ThreadData& thread,
            size_t index, int64_t begin, int64_t end);
        void Touch(uint32_t node, const tracy::ZoneEvent& zone, int64_t time, int64_t offset);
        void SortChildren();
        void UpdateSelected(const tracy::Worker& worker, const ViewState& view);
        void UpdateTrend(const tracy::Worker& worker, const FrameSnapshot& frames, const ViewState& view);
        int64_t PathValue(const tracy::Worker& worker, size_t frame) const;

        std::vector<UpdateNode> nodes_;
        std::unordered_map<uint64_t, uint32_t> childMap_; ///< （親の番号・区間の場所）→ 節
        std::vector<Scratch> scratch_;
        std::vector<uint32_t> touched_;

        std::vector<FrameClass> frameClass_;              ///< 数えたフレームの見分け（フレームの番号で引く）
        std::vector<const tracy::ZoneEvent*> frameRoot_;  ///< 数えたフレームの Update の区間
        std::vector<int64_t> rootTime_;                   ///< 数えたフレームの Update の時間（無ければ -1）
        std::array<uint64_t, static_cast<size_t>(FrameClass::Count)> frameCount_ = {};
        std::vector<size_t> firstPlayFrames_;
        FrameClass lastClass_ = FrameClass::None;         ///< Update の区間があった最後のフレームの見分け
        size_t available_ = 0;
        bool orderDirty_ = false;

        const tracy::ThreadData* mainThread_ = nullptr;
        uint64_t mainThreadId_ = 0;
        bool hasRootSrcloc_ = false;
        int16_t rootSrcloc_ = 0;

        bool selectedValid_ = false;
        size_t selectedFrame_ = 0;
        size_t selectedNodeCount_ = 0;
        std::vector<int64_t> selectedTime_;
        std::vector<int64_t> selectedChild_;

        uint32_t wantTrendNode_ = kNoNode;
        bool wantTrendSelf_ = false;
        uint32_t trendNode_ = kNoNode;
        bool trendSelf_ = false;
        std::vector<uint32_t> trendPath_; ///< 根から推移の節までの節の番号
        size_t trendFirst_ = 0;
        std::vector<int64_t> trendValues_;
    };
}
