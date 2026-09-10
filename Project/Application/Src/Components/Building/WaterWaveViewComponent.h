#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CoreEngine {
    class Camera;
    class GameObject;
    class MeshRendererComponent;
    class TransformComponent;
}

namespace GameComponents {
    class MapGeneratorComponent;
    class WaterFallShaderProvider;
    class WaterWaveShaderProvider;
}

namespace GameComponents
{
    /// @brief 水マスへ細分割した板を並べ、頂点シェーダーで波打たせるコンポーネント
    /// @details 板の頂点は WaterWave.VS.hlsl が「ワールド座標と時刻」だけから変位させる。
    ///          マスごとに別の板でも、隣り合う縁の頂点は同じ値になるので継ぎ目が出ない。
    ///          そのため水域が地形上に散らばっていても、1 枚の巨大な板を用意する必要がない。
    /// @note MapViewComponent の水描画（潰した box.obj）と二重に出さないこと。
    ///       GameScene では MapViewComponent へ水プールを渡さず、こちらへ任せている。
    class WaterWaveViewComponent final : public CoreEngine::IComponent {
    public:
        explicit WaterWaveViewComponent(
            MapGeneratorComponent* mapGenerator,
            CoreEngine::Camera* viewCamera,
            float gridSize = 1.0f,
            uint32_t viewDistanceX = 30,
            std::size_t initialCapacity = 100);

        ~WaterWaveViewComponent() override;

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "WaterWaveView";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "水面の波"; }
        bool DrawInspector() override;
#endif

        /// @brief シェーダーと板のプールを用意する
        void Awake() override;
        /// @brief 波を進めて、描画範囲内の水マスへ板を配る
        void Update() override;
        /// @brief 生成した板を残さない
        void OnDestroy() override;

    private:
        /// @brief 板 1 枚ぶんの実体
        struct Entry {
            CoreEngine::GameObject* object = nullptr;
            CoreEngine::TransformComponent* transform = nullptr;
            // マテリアルは MaterialComponent を介さず直接触る（理由は CreateEntry のコメント）
            CoreEngine::MeshRendererComponent* renderer = nullptr;
            std::uint64_t lastSubmittedFrame =
                (std::numeric_limits<std::uint64_t>::max)();
        };

        /// @brief 板の種類。メッシュ・シェーダー・見た目の作り方が変わる
        enum class PlaneKind {
            Surface, ///< 水面。水平に敷いて Gerstner 波で上下させる
            Fall,    ///< 落水カーテン。マップ端から垂直に垂らす
        };

        /// @brief 同じ作り方の板をまとめた 1 つのプール
        /// @details 水面板と落水カーテンで、割り当てと使い回しの手順は完全に同じ。
        ///          違うのはメッシュ・シェーダー・置き方だけなので、状態はこの型で
        ///          まとめて持ち、種類は PlaneKind で切り替える。
        struct PlanePool {
            std::vector<Entry> entries;

            /// @brief 位置キー → entries の添字（今フレーム分と前フレーム分）
            /// @details 同じマスを毎フレーム同じ板へ割り当てるために持つ。
            ///          呼び出し順で先頭から配ると、描画範囲が 1 マスずれた瞬間に
            ///          全板の担当マスがずれ、画面は静止して見えるのに板だけが飛ぶ。
            ///          その「飛び」はモーションベクターへそのまま出るので TAA がぶれる。
            std::unordered_map<std::uint64_t, std::size_t> entryByPosition;
            std::unordered_map<std::uint64_t, std::size_t> prevEntryByPosition;

            std::size_t nextEntryIndex = 0;
            std::uint64_t allocationFrame =
                (std::numeric_limits<std::uint64_t>::max)();
            std::uint64_t lastExhaustedWarningFrame =
                (std::numeric_limits<std::uint64_t>::max)();
        };

        Entry* CreateEntry(PlanePool& pool, PlaneKind kind);
        /// @brief フレームが変わっていたら割り当て状態を繰り越す
        void BeginFrameIfNeeded(PlanePool& pool, std::uint64_t frame);
        /// @brief 今フレームまだ使っていない板を 1 枚借りる（前フレームの担当を優先）
        Entry* AcquireEntry(
            PlanePool& pool, PlaneKind kind,
            std::uint64_t positionKey, std::uint64_t frame);
        /// @brief このフレームに配られなかった板を隠す
        void HideUnusedEntries(PlanePool& pool, std::uint64_t frame);
        /// @brief 指定マスへ水面の板を 1 枚出す
        void DrawCell(float worldX, float worldZ, std::uint64_t frame);
        /// @brief マップ端へ落水カーテンを 1 枚垂らす
        /// @param edgeZ マスの外側の縁のワールド Z
        /// @param facingNegativeZ 手前端（-Z を向く）なら true、奥端なら false
        void DrawFall(
            float worldX, float edgeZ, bool facingNegativeZ, std::uint64_t frame);
        /// @brief 波パラメータを組み立てて GPU へ転送する
        void UploadWaveConstants();
        /// @brief 板 1 枚へ色・粗さを反映する
        /// @note 水面板と落水カーテンで同じ値を入れる（理由は定義側のコメント）
        void ApplyMaterialToEntry(Entry& entry);
        /// @brief 現在の色・粗さを全ての板へ反映する（インスペクタで触ったとき用）
        void ApplyMaterialToEntries();
        /// @brief 静止水面のワールド Y を求める
        float GetWaterSurfaceHeight() const;
        /// @brief 落水カーテンの縦の長さ[m]を求める
        float GetFallLength() const;

        float gridSize_ = 1.0f;
        uint32_t viewDistanceX_ = 30;
        std::size_t initialCapacity_ = 100;

        // ===== 見た目の調整値 =====
        /// @brief 波の高さ倍率。0 で完全な平面へ戻る
        float waveHeightScale_ = 1.0f;
        /// @brief 波の速さ倍率
        float waveSpeedScale_ = 1.0f;
        /// @brief 静止水面の高さ。マスの底（-gridSize/2）からの割合で持つ
        /// @details 既定の 0.65 は、置き換え前の「潰した box.obj」の上面と同じ高さ。
        float waterLevelRatio_ = 0.65f;
        /// @brief 水面の粗さ
        /// @details つるつる（0.1 以下）にすると、法線が傾いても一様な空を映すだけなので
        ///          波が陰影に出ない。ざらつかせて拡散光を効かせるほうが波として読める。
        float waterRoughness_ = 0.30f;
        /// @brief 水面の色（置き換え前の box.obj の水と同じ値）
        /// @note α は 1 のままでよい。フォワード経路へ回すのは MeshRenderer のブレンド指定で
        ///       やっているので、透過させるために α を下げる必要は無い。
        CoreEngine::Vector4 waterColor_{ 0.0f, 0.35f, 0.65f, 1.0f };

        // ===== マップ端の滝 =====
        /// @brief マップの Z 両端にある水マスから滝を垂らすか
        bool fallEnabled_ = true;
        /// @brief 落差。マス何個ぶん下まで落とすか
        /// @details 端の水マスは地面ブロックを持たないので、下は素通しの空間になる。
        ///          短いと板の切れ端に見えるので、下端が霧散しきる長さを取ること。
        float fallLengthRatio_ = 6.0f;
        /// @brief 流れ落ちる速さ[マス/秒]
        float fallFlowSpeed_ = 3.0f;
        /// @brief 白泡の強さ。0 で泡が消え、ただの water 色の帯になる
        float fallFoamStrength_ = 1.0f;

        float elapsedTime_ = 0.0f;

        MapGeneratorComponent* mapGenerator_ = nullptr;
        // 描画範囲はゲーム視点カメラの位置から決める（MapViewComponent と同じ基準）
        CoreEngine::Camera* viewCamera_ = nullptr;

        std::unique_ptr<WaterWaveShaderProvider> shaderProvider_;
        std::unique_ptr<WaterFallShaderProvider> fallShaderProvider_;

        PlanePool surfacePool_;
        PlanePool fallPool_;
    };
}
