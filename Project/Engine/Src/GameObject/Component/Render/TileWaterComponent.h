#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class GameObject;
    class MeshRendererComponent;
    class TransformComponent;
    class TileWaterWaveShaderProvider;
    class TileWaterFallShaderProvider;

    /// @brief 1 マス 1 枚の板を並べ、頂点シェーダーで波打たせる水面
    /// @details 板の頂点は WaterWave.VS.hlsl が「ワールド座標と時刻」だけから変位させる。
    ///          マスごとに別の板でも、隣り合う縁の頂点は同じ値になるので継ぎ目が出ない。
    ///          表示したい板は毎フレーム DrawSurface() / DrawFall() する。呼ばれなかった板は Update で隠す。
    class TileWaterComponent final : public IComponent {
    public:
        TileWaterComponent();
        explicit TileWaterComponent(std::size_t initialCapacity);
        ~TileWaterComponent() override;

        const char* GetTypeName() const override {
            return "TileWater";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "タイルの水面"; }
        bool DrawInspector() override;
#endif

        /// @brief シェーダーと板のプールを用意する
        void Awake() override;
        /// @brief 波を進めて GPU へ送り、このフレームに描かれなかった板を隠す
        void Update() override;
        /// @brief 生成した板を残さない
        void OnDestroy() override;

        /// @brief このフレームに水面の板を 1 枚出す
        /// @param worldX 板の中心のワールド X
        /// @param worldZ 板の中心のワールド Z
        void DrawSurface(float worldX, float worldZ);

        /// @brief このフレームに落水カーテンを 1 枚垂らす
        /// @param worldX 板の中心のワールド X
        /// @param edgeZ 水面の外側の縁のワールド Z
        /// @param facingNegativeZ -Z を向けるなら true、+Z を向けるなら false
        void DrawFall(float worldX, float edgeZ, bool facingNegativeZ);

        float GetTileSize() const { return tileSize_; }
        void SetTileSize(float size);
        float GetSurfaceHeight() const { return surfaceHeight_; }
        void SetSurfaceHeight(float height) { surfaceHeight_ = height; }
        float GetWaveHeightScale() const { return waveHeightScale_; }
        void SetWaveHeightScale(float scale);
        float GetWaveSpeedScale() const { return waveSpeedScale_; }
        void SetWaveSpeedScale(float scale);
        const Vector4& GetWaterColor() const { return waterColor_; }
        float GetWaterRoughness() const { return waterRoughness_; }
        /// @brief 水の色と粗さを全ての板へ反映する
        void SetWaterMaterial(const Vector4& color, float roughness);
        float GetFallLengthRatio() const { return fallLengthRatio_; }
        void SetFallLengthRatio(float ratio);
        float GetFallFlowSpeed() const { return fallFlowSpeed_; }
        void SetFallFlowSpeed(float speed);
        float GetFallFoamStrength() const { return fallFoamStrength_; }
        void SetFallFoamStrength(float strength);

        std::size_t GetSurfaceTileCount() const { return surfacePool_.entries.size(); }
        std::size_t GetFallTileCount() const { return fallPool_.entries.size(); }

    private:
        /// @brief 板 1 枚ぶんの実体
        struct Entry {
            GameObject* object = nullptr;
            TransformComponent* transform = nullptr;
            // マテリアルは MaterialComponent を介さず直接触る（理由は CreateEntry のコメント）
            MeshRendererComponent* renderer = nullptr;
            std::uint64_t lastSubmittedFrame =
                (std::numeric_limits<std::uint64_t>::max)();
        };

        /// @brief 板の種類。メッシュ・シェーダー・見た目の作り方が変わる
        enum class PlaneKind {
            Surface, ///< 水面。水平に敷いて Gerstner 波で上下させる
            Fall,    ///< 落水カーテン。垂直に垂らす
        };

        /// @brief 同じ作り方の板をまとめた 1 つのプール
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
        /// @brief 波パラメータを組み立てて GPU へ転送する
        void UploadWaveConstants();
        /// @brief 板 1 枚へ色・粗さを反映する
        void ApplyMaterialToEntry(Entry& entry);
        /// @brief 現在の色・粗さを全ての板へ反映する
        void ApplyMaterialToEntries();
        /// @brief 落水カーテンの縦の長さ[m]を求める
        float GetFallLength() const;

        /// @brief 板 1 枚の一辺[m]。波の形もこの大きさへ合わせる
        float tileSize_ = 1.0f;
        /// @brief 静止水面のワールド Y
        float surfaceHeight_ = 0.0f;
        /// @brief 水面の板を先に作っておく枚数
        std::size_t initialCapacity_ = 100;

        /// @brief 波の高さ倍率。0 で完全な平面へ戻る
        float waveHeightScale_ = 1.0f;
        /// @brief 波の速さ倍率
        float waveSpeedScale_ = 1.0f;
        /// @brief 水面の粗さ
        float waterRoughness_ = 0.30f;
        /// @brief 水面の色
        Vector4 waterColor_{ 0.0f, 0.35f, 0.65f, 1.0f };

        /// @brief 落差。板何枚ぶん下まで落とすか
        float fallLengthRatio_ = 6.0f;
        /// @brief 流れ落ちる速さ[板/秒]
        float fallFlowSpeed_ = 3.0f;
        /// @brief 白泡の強さ。0 で泡が消える
        float fallFoamStrength_ = 1.0f;

        float elapsedTime_ = 0.0f;

        std::unique_ptr<TileWaterWaveShaderProvider> shaderProvider_;
        std::unique_ptr<TileWaterFallShaderProvider> fallShaderProvider_;

        PlanePool surfacePool_;
        PlanePool fallPool_;
    };
}
