#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    /// @brief 音を聞く位置を表すコンポーネント
    /// @details `AudioSourceComponent` の「距離で変える」が有効なとき、ここからの距離で
    ///          音量を、ここから見た左右で振り分けを決める。ふつうはカメラに付ける。
    /// @note シーンに 1 つだけ置くこと。2 つ目からは警告を出して無視する。
    class AudioListenerComponent : public IComponent
    {
    public:
        const char* GetTypeName() const override { return "AudioListener"; }

        REFLECT_BEGIN(AudioListenerComponent, "音の聞き手")
        REFLECT_END()

        /// @brief 自分を聞き手として名乗り出る（先に居れば譲る）
        void Awake() override;

        /// @brief 聞き手の座を降りる
        void OnDestroy() override;

        /// @brief 今の聞き手（居なければ nullptr）
        static AudioListenerComponent* GetActive() { return active_; }

        /// @brief 聞き手の位置
        Vector3 GetWorldPosition() const;

        /// @brief 聞き手から見た右向き（振り分けの基準）
        Vector3 GetRightAxis() const;

    private:
        /// 先に名乗り出たもの。OnDestroy で降りる
        static AudioListenerComponent* active_;
    };
}
