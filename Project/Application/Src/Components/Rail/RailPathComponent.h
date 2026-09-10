#pragma once

#include "GameObject/Component/Core/IComponent.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace GameComponents
{
    struct RailUndoResult {
        bool succeeded = false;
        std::pair<int32_t, int32_t> removedPosition = { -1, -1 };
        std::pair<int32_t, int32_t> builderPosition = { -1, -1 };
        float refundAmount = 0.0f;
    };

    // レールの配置、撤去を管理するコンポーネント
    class RailPathComponent final
        : public CoreEngine::IComponent {
    public:
        explicit RailPathComponent(
            uint32_t mapSizeZ = 10,
            uint32_t startX = 0, uint32_t startZ = 0);

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "RailPath";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "レール経路"; }
        bool DrawInspector() override;
#endif

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;

        // レールを設置する
        bool PlaceRail(int32_t x, int32_t z, float refundableStaminaCost);
        // 最後のUndo可能なレールを撤去し、設置時に消費したレール数を返す
        RailUndoResult UndoLastRailPlacement();

        // キューにあるレールの先頭を確定する
        bool ConfirmNextRailPlacement();
        // 現在設置済みの未確定レールをすべて確定する。列車の走行経路は保持する
        void ConfirmAllPendingRailPlacements();

        // 次に列車が進む未確定レールを取得する。存在しなければ false
        bool TryGetNextUnconfirmedRail(std::pair<int32_t, int32_t>& destination) const;
        // 列車がこれから走るレールの数を取得する
        std::size_t GetUnconfirmedRailCount() const;

        // レールマップを取得する
        std::vector<std::pair<int32_t, int32_t>>& GetRailMap();
        // Z方向のマップサイズを取得する。X正方向には上限を設けない
        uint32_t GetMapSizeZ() const;
        // 初期位置を除いた、現在までに敷設されたレールの長さを取得する。
        // 確定済みレールと未確定レールの両方を含む。
        std::size_t GetLaidRailCount() const;

        // Undoスタックを取得する
        const std::vector<std::pair<int32_t, int32_t>>& GetRailUndoStack() const;

    private:
        uint32_t mapSizeZ_ = 10;
        uint32_t startX_ = 0;
        uint32_t startZ_ = 0;

        // 確定したレールの座標を保持するマップ（2D配列）
        std::vector<std::pair<int32_t, int32_t>> railMap_;

        // Undo/Redo スタック
        std::vector<std::pair<int32_t, int32_t>> railUndoStack_;
        std::vector<float> railUndoCosts_;

        // 駅でレールが確定された後も、列車が走行できるよう経路を独立して保持する
        std::vector<std::pair<int32_t, int32_t>> trainRouteQueue_;
    };
}
