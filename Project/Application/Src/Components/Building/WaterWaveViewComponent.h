#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

#include <cstddef>
#include <cstdint>

namespace CoreEngine {
    class TileWaterComponent;
}

namespace GameComponents {
    class MapGeneratorComponent;
}

namespace GameComponents
{
    /// @brief 描画範囲の水マスへ水面の板を配り、マップの Z 両端の水マスから滝を垂らすコンポーネント
    /// @details 板の描画は、Awake で同じオブジェクトへ足す TileWaterComponent が行う。
    /// @note MapViewComponent の水描画（潰した box.obj）と二重に出さないこと。
    ///       GameScene では MapViewComponent へ水プールを渡さず、こちらへ任せている。
    class WaterWaveViewComponent final : public CoreEngine::IComponent {
    public:
        explicit WaterWaveViewComponent(
            MapGeneratorComponent* mapGenerator,
            float gridSize = 1.0f,
            uint32_t viewDistanceX = 30,
            std::size_t initialCapacity = 100);

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "WaterWaveView";
        }

        REFLECT_DECLARE(WaterWaveViewComponent)

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "水面の波"; }
        bool DrawInspector() override;
#endif

        /// @brief 板を描く TileWaterComponent を足して設定を渡す
        void Awake() override;
        /// @brief 描画範囲内の水マスへ板を配る
        void Update() override;

    private:
        /// @brief 自分の設定を TileWaterComponent へ渡す
        void ApplySettingsToTiles();
        /// @brief 静止水面のワールド Y を求める
        float GetWaterSurfaceHeight() const;

        float gridSize_ = 1.0f;
        uint32_t viewDistanceX_ = 30;
        std::size_t initialCapacity_ = 100;

        // ===== 見た目の調整値 =====
        /// @brief 波の高さ倍率。0 で完全な平面へ戻る
        float waveHeightScale_ = 1.0f;
        /// @brief 波の速さ倍率
        float waveSpeedScale_ = 1.0f;
        /// @brief 静止水面の高さ。マスの底（-gridSize/2）からの割合で持つ
        float waterLevelRatio_ = 0.65f;
        /// @brief 水面の粗さ
        float waterRoughness_ = 0.30f;
        /// @brief 水面の色
        CoreEngine::Vector4 waterColor_{ 0.0f, 0.35f, 0.65f, 1.0f };

        // ===== マップ端の滝 =====
        /// @brief マップの Z 両端にある水マスから滝を垂らすか
        bool fallEnabled_ = true;
        /// @brief 落差。マス何個ぶん下まで落とすか
        float fallLengthRatio_ = 6.0f;
        /// @brief 流れ落ちる速さ[マス/秒]
        float fallFlowSpeed_ = 3.0f;
        /// @brief 白泡の強さ。0 で泡が消え、ただの water 色の帯になる
        float fallFoamStrength_ = 1.0f;

        CoreEngine::ObjectRef<MapGeneratorComponent> mapGenerator_;

        /// @brief 板を描くコンポーネント（持ち主は同じ GameObject）
        CoreEngine::TileWaterComponent* tiles_ = nullptr;
    };
}
