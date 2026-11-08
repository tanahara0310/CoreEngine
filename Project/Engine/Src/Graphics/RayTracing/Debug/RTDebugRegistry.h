#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

#include "Graphics/RayTracing/RayTracingDispatchInfo.h"

namespace CoreEngine
{
    class GameObjectManager;
    class GpuResource;
    class GraphicsCore;
    class LightManager;
    class RenderDomainContext;
    class RTDebugSystem;
    struct RenderContext;

    /// @brief 中間結果の色の付け方
    enum class RTDebugViewMode : uint32_t {
        Mono = 0,    ///< 1 成分を白黒（範囲の下限 = 黒・上限 = 白）
        FalseColor,  ///< 1 成分を疑似カラー（青 → 緑 → 黄 → 赤）
        Heat,        ///< 1 成分をヒートマップ（0 以下は黒・緑 → 黄 → 赤）
        Signed,      ///< 1 成分を正負で色分け（負 = 青・正 = 赤。絶対値が下限以下は黒・上限で飽和）
        Normal,      ///< RGB を法線として（-1〜1 を 0〜1 へ）
        Color,       ///< 選んだ成分をそのまま（範囲で 0〜1 へ）
        Id,          ///< 1 成分の整数を番号ごとの色に（負は黒）
        Bits,        ///< 1 成分の整数の立っているビットを色の帯に
        Count
    };

    /// @brief 色の付け方の表示名
    const char* ToDisplayName(RTDebugViewMode mode);

    /// @brief 1 成分だけを使う色の付け方か（法線と色そのままは複数の成分を使う）
    bool UsesSingleComponent(RTDebugViewMode mode);

    /// @brief 中間結果 1 枚の実体
    struct RTDebugSource {
        GpuResource* resource = nullptr;      ///< 実体（ステート追跡つき）
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};    ///< シェーダから読む SRV（Texture2D）
    };

    /// @brief 中間結果の申告（デバッグ窓の「中間結果」タブに並ぶ）
    struct RTDebugChannelDesc {
        const char* effect = "";              ///< 効果の名前（木の 1 段目）
        const char* stage = "";               ///< 段の名前（木の 2 段目）
        const char* name = "";                ///< 中身の名前
        const char* description = "";         ///< 説明（ツールチップ）
        const char* components[4] = {};       ///< 成分ごとの意味（nullptr = その成分は使わない）
        float rangeMin = 0.0f;                ///< 既定の範囲の下限
        float rangeMax = 1.0f;                ///< 既定の範囲の上限
        RTDebugViewMode viewMode = RTDebugViewMode::Mono; ///< 既定の色の付け方
        uint32_t component = 0;               ///< 既定で見る成分（1 成分の色の付け方のとき）
        bool screenSpace = true;              ///< 画面と同じ並びか（ゲーム画面へ重ねられる）
        bool alphaMarksMiss = false;          ///< A = 0 の画素を「当たっていない」として模様で出す
        uint32_t variantCount = 1;            ///< 同じ中身の数（影ならライトの数）
        const char* variantLabel = "";        ///< 変種の呼び名（"ライト" など）
        /// @brief 今フレームの実体を引く（描画の途中で呼ばれる。今フレームに作っていなければ false）
        bool (*resolve)(const RenderContext& context, uint32_t variant, RTDebugSource& out) = nullptr;
    };

    /// @brief デバッグ窓が効果やチェックへ渡す入口
    struct RTDebugQuery {
        RenderDomainContext* domain = nullptr;      ///< 描画ドメイン（マネージャの取り口）
        GraphicsCore* graphics = nullptr;           ///< 記述子ヒープの大きさなど
        LightManager* lights = nullptr;             ///< ライト（無ければ nullptr）
        const GameObjectManager* objects = nullptr; ///< 今のシーンの物（無ければ nullptr）
        const RTDebugSystem* debug = nullptr;       ///< GPU で調べた結果（無ければ nullptr）
        uint64_t frame = 0;                         ///< 今のフレーム番号
    };

    /// @brief 効果の 1 行（影ならライトごと）
    struct RTDebugEffectRow {
        std::string label;                     ///< 行の名前
        RayTracingDispatchInfo info{};         ///< 直近のディスパッチの診断情報
    };

    /// @brief 効果の申告（デバッグ窓の「概要」タブに並ぶ）
    struct RTDebugEffectDesc {
        const char* id = "";                   ///< 一意の名前
        const char* label = "";                ///< 表示名
        const char* cvarPrefix = nullptr;      ///< 設定の CVar の接頭辞（無ければ nullptr）
        const char* passNames[4] = {};         ///< GPU 時間を足すパスの名前（RenderGraph の名前）
        const char* stageLabels[4] = {};       ///< passNames の段の表示名
        /// @brief 行を集める（無ければ nullptr）
        void (*collect)(const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) = nullptr;
    };

    /// @brief チェックの結果の重さ
    enum class RTDebugSeverity : uint8_t {
        Ok = 0,   ///< 良し
        Info,     ///< 情報
        Warning,  ///< 警告
        Error,    ///< 誤り
    };

    /// @brief チェックの結果 1 件
    struct RTDebugCheckResult {
        RTDebugSeverity severity = RTDebugSeverity::Ok;
        std::string message;
    };

    /// @brief チェックの申告（デバッグ窓の「チェック」タブで回る）
    struct RTDebugCheckDesc {
        const char* name = "";                 ///< チェックの名前
        void (*run)(const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) = nullptr;
    };

#ifdef CORE_EDITOR
    /// @brief 申告した効果の取り口（パスが「止めていないか」を聞く）
    class RTDebugEffect {
    public:
        /// @brief 効果を撃ってよいか（窓で止めていない・全部止めるが切れている）
        bool IsActive() const;
        /// @brief 登録の番号
        uint32_t Index() const { return index_; }

    private:
        friend class RTDebugRegistry;
        explicit RTDebugEffect(uint32_t index) : index_(index) {}
        uint32_t index_ = 0;
    };

    /// @brief 効果・中間結果・チェックの申告を集める
    /// @details 申告は各 .cpp から RT_DEBUG_EFFECT_REGISTER / RT_DEBUG_CHANNEL_REGISTER /
    ///          RT_DEBUG_CHECK_REGISTER で行う。デバッグ窓は申告を並べるだけで、効果の種類を知らない。
    class RTDebugRegistry {
    public:
        /// @brief 登録済みの効果 1 つ
        struct EffectEntry {
            RTDebugEffectDesc desc;
            bool enabled = true; ///< 窓の「ON」
        };

        static RTDebugRegistry& Get();

        RTDebugEffect RegisterEffect(const RTDebugEffectDesc& desc);
        void RegisterChannel(const RTDebugChannelDesc& desc);
        void RegisterCheck(const RTDebugCheckDesc& desc);

        const std::vector<EffectEntry>& Effects() const { return effects_; }
        const std::vector<RTDebugChannelDesc>& Channels() const { return channels_; }
        const std::vector<RTDebugCheckDesc>& Checks() const { return checks_; }

        /// @brief 効果を撃つか（窓の「ON」）
        void SetEffectEnabled(uint32_t index, bool enabled);
        bool IsEffectEnabled(uint32_t index) const;

        /// @brief 名前で中間結果を探す（見つからなければ -1）
        int FindChannel(const char* effect, const char* name) const;

    private:
        std::vector<EffectEntry> effects_;
        std::vector<RTDebugChannelDesc> channels_;
        std::vector<RTDebugCheckDesc> checks_;
    };

    /// @brief 切り分けのスイッチ（CVar r.RT.Debug.*。保存しない）
    namespace RTDebug {
        /// @brief TLAS と材質の表を作り直さず、止めた時点の物のまま撃つか
        bool IsSceneFrozen();
        /// @brief 乱数の番号を毎フレーム同じにするか
        bool IsNoiseFixed();
        /// @brief 乱数に使うフレームの番号（固定なら 0）
        uint32_t NoiseFrame(uint32_t frame);
        /// @brief レイトレーシングの効果を全部止めるか
        bool IsStopped();
        void SetSceneFrozen(bool frozen);
        void SetNoiseFixed(bool fixed);
        void SetStopped(bool stopped);
    }

    /// @brief フレームの掲示板に載った画像を中間結果として引く（今フレームに載っていなければ false）
    bool ResolveFrameBlackboardImage(const RenderContext& context, const char* name, RTDebugSource& out);
#else
    /// @brief 申告した効果の取り口（ゲームの書き出しでは常に撃つ）
    class RTDebugEffect {
    public:
        constexpr bool IsActive() const { return true; }
    };

    namespace RTDebug {
        constexpr bool IsSceneFrozen() { return false; }
        constexpr bool IsNoiseFixed() { return false; }
        constexpr uint32_t NoiseFrame(uint32_t frame) { return frame; }
        constexpr bool IsStopped() { return false; }
    }
#endif
}

#define RT_DEBUG_DETAIL_CONCAT_INNER(a, b) a##b
#define RT_DEBUG_DETAIL_CONCAT(a, b) RT_DEBUG_DETAIL_CONCAT_INNER(a, b)

#ifdef CORE_EDITOR
/// @brief 効果を申告し、取り口の変数を作る（.cpp のファイルスコープに書く）
/// @param Variable 取り口の変数名（IsActive() で止めていないかを聞く）
#define RT_DEBUG_EFFECT_REGISTER(Variable, ...)                                                   \
    const ::CoreEngine::RTDebugEffect Variable =                                                  \
        ::CoreEngine::RTDebugRegistry::Get().RegisterEffect(::CoreEngine::RTDebugEffectDesc{ __VA_ARGS__ })

/// @brief 中間結果を申告する（.cpp のファイルスコープに書く）
#define RT_DEBUG_CHANNEL_REGISTER(...)                                                            \
    namespace {                                                                                   \
        const bool RT_DEBUG_DETAIL_CONCAT(rtDebugChannel_, __LINE__) =                            \
            (::CoreEngine::RTDebugRegistry::Get().RegisterChannel(                                \
                ::CoreEngine::RTDebugChannelDesc{ __VA_ARGS__ }), true);                          \
    }

/// @brief チェックを申告する（.cpp のファイルスコープに書く）
#define RT_DEBUG_CHECK_REGISTER(...)                                                              \
    namespace {                                                                                   \
        const bool RT_DEBUG_DETAIL_CONCAT(rtDebugCheck_, __LINE__) =                              \
            (::CoreEngine::RTDebugRegistry::Get().RegisterCheck(                                  \
                ::CoreEngine::RTDebugCheckDesc{ __VA_ARGS__ }), true);                            \
    }
#else
#define RT_DEBUG_EFFECT_REGISTER(Variable, ...) constexpr ::CoreEngine::RTDebugEffect Variable{}
#define RT_DEBUG_CHANNEL_REGISTER(...)
#define RT_DEBUG_CHECK_REGISTER(...)
#endif
