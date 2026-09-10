#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class GameObject;
    class MaterialComponent;
    class TransformComponent;
}

namespace GameComponents
{
    /// @brief Draw() が呼ばれた分だけ、同じ静的モデルの GameObject をプールから表示する。
    /// @note 表示したいモデルは毎フレーム Draw() する。呼ばれなかった要素は自動で非表示になる。
    class ModelRenderPoolComponent final : public CoreEngine::IComponent {
    public:
        explicit ModelRenderPoolComponent(
            std::string modelPath,
            std::size_t initialCapacity = 32,
            bool allowGrowth = true,
            std::optional<CoreEngine::Vector4> color = std::nullopt)
            : modelPath_(std::move(modelPath)),
              initialCapacity_(initialCapacity),
              allowGrowth_(allowGrowth),
              color_(color) {
        }

        const char* GetTypeName() const override {
            return "ModelRenderPool";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "モデルプール"; }
        bool DrawInspector() override;
#endif

        /// @brief initialCapacity 分のオブジェクトを生成する。
        void Awake() override;
        /// @brief このフレームに Draw() されなかった要素を非表示にする。
        void Update() override;
        /// @brief コンポーネントだけが外された場合も、生成したオブジェクトを残さない。
        void OnDestroy() override;

        /// @brief このフレームにモデルを1つ表示する。
        /// @param color この1回だけのベースカラー。省略するとプール共通の色（未設定なら白）に戻す。
        /// @return 描画用オブジェクトを割り当てられた場合 true。
        /// @note 色はモデルのベースカラーへ乗算される。プールの要素は毎フレーム別のマスへ
        ///       割り当てられうるので、マスごとに色を変えたい場合は毎回渡すこと。
        bool Draw(
            const CoreEngine::Vector3& position,
            const CoreEngine::Vector3& rotation = { 0.0f, 0.0f, 0.0f },
            const CoreEngine::Vector3& scale = { 1.0f, 1.0f, 1.0f },
            const std::optional<CoreEngine::Vector4>& color = std::nullopt);

        std::size_t GetCapacity() const { return entries_.size(); }
        std::size_t GetActiveCount() const;
        std::size_t GetAvailableCount() const {
            return GetCapacity() - GetActiveCount();
        }

    private:
        struct Entry {
            CoreEngine::GameObject* object = nullptr;
            CoreEngine::TransformComponent* transform = nullptr;
            CoreEngine::MaterialComponent* material = nullptr;
            /// @brief 今この要素へ入っている色。同じ値の再設定を省くために持つ。
            std::optional<CoreEngine::Vector4> appliedColor;
            std::uint64_t lastSubmittedFrame =
                (std::numeric_limits<std::uint64_t>::max)();
        };

        Entry* CreateEntry();

        /// @brief 要素へ色を反映する（変化が無ければ何もしない）
        void ApplyEntryColor(Entry& entry, const std::optional<CoreEngine::Vector4>& color);

        /// @brief フレームが変わっていたら割り当て状態を繰り越す
        void BeginFrameIfNeeded(std::uint64_t frame);

        /// @brief 前フレームに同じ場所を描いた要素を返す（無ければ nullptr）
        Entry* FindEntryForPosition(std::uint64_t positionKey, std::uint64_t frame);

        Entry* FindAvailableEntry(std::uint64_t frame);
        void ResizePool(std::size_t capacity);
        void ApplyColorToEntries();

        std::string modelPath_;
        std::size_t initialCapacity_ = 32;
        bool allowGrowth_ = true;
        std::optional<CoreEngine::Vector4> color_;
        std::uint64_t allocationFrame_ =
            (std::numeric_limits<std::uint64_t>::max)();
        std::size_t nextEntryIndex_ = 0;
        std::uint64_t lastExhaustedWarningFrame_ =
            (std::numeric_limits<std::uint64_t>::max)();
        std::vector<Entry> entries_;

        /// @brief 位置キー → entries_ の添字（今フレーム分と前フレーム分）
        /// @details 同じ場所を毎フレーム同じ要素へ割り当てるために持つ。
        ///          呼び出し順で先頭から配ると、描画範囲が 1 マスずれた瞬間に
        ///          全要素の担当がずれ、画面は静止して見えるのに
        ///          オブジェクトだけが 1 マス飛ぶ。GBuffer のモーションベクターは
        ///          その「飛び」をそのまま出すので、RT シャドウのテンポラル再投影が
        ///          誤った履歴を拾って影がちらつく。
        std::unordered_map<std::uint64_t, std::size_t> entryByPosition_;
        std::unordered_map<std::uint64_t, std::size_t> prevEntryByPosition_;
    };
}
