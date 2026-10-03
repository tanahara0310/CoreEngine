#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

#include <vector>

namespace CoreEngine
{
    /// @brief 音を聞く位置を表すコンポーネント
    /// @details `AudioSourceComponent` の「距離で変える」が有効なとき、ここからの距離で
    ///          音量を、ここから見た左右で振り分けを決める。ふつうはカメラに付ける。
    /// @note 有効なものが 2 つ以上あると警告を出し、先に有効になった方を使う（それを無効にすると次のものへ移る）。
    class AudioListenerComponent : public IComponent
    {
    public:
        const char* GetTypeName() const override { return "AudioListener"; }

        REFLECT_BEGIN(AudioListenerComponent, "音の聞き手")
        REFLECT_END()

        /// @brief 聞き手の候補に並ぶ（先に並んだものが聞き手）
        void OnEnable() override;

        /// @brief 候補から外れる（次に並んでいるものが聞き手になる）
        void OnDisable() override;

        /// @brief 今の聞き手（居なければ nullptr）
        static AudioListenerComponent* GetActive();

        /// @brief 聞き手の位置
        Vector3 GetWorldPosition() const;

        /// @brief 聞き手から見た右向き（振り分けの基準）
        Vector3 GetRightAxis() const;

    private:
        /// 有効な聞き手（有効になった順。先頭が聞き手）
        static std::vector<AudioListenerComponent*> enabledListeners_;
    };
}
