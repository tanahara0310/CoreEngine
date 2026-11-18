#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    class SpriteRendererComponent;

    /// @brief スプライトアニメの 1 本（コマの範囲と速さ）
    struct SpriteAnimationClip {
        std::string name;      ///< Play で指す名前
        int firstFrame = 0;    ///< 最初のコマ
        int lastFrame = 0;     ///< 最後のコマ（最初より小さければ逆に進む）
        float fps = 10.0f;     ///< 1 秒に進むコマの数
        bool loop = true;      ///< 最後のコマの後で最初へ戻るか
    };

    /// @brief 兄弟のスプライト描画のコマを、クリップに沿って進めるコンポーネント
    /// @details クリップはスプライトのコマ割り（横と縦のコマ数）の番号で書く。
    ///          再生中だけ進み、ループしないクリップは最後のコマで止まる。
    ///          アニメーターに動かされている間は自分では進まず、ShowClipAt で渡されたクリップと時刻のコマを出す。
    class SpriteAnimatorComponent : public IComponent
    {
    public:
        const char* GetTypeName() const override { return "SpriteAnimator"; }

        REFLECT_DECLARE(SpriteAnimatorComponent)

        /// @brief スプライト描画を使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief 最初のクリップを流し始める
        void Start() override;

        /// @brief 時間を進め、今のコマをスプライトへ写す
        void Update() override;

        // ===== 再生 =====

        /// @brief クリップを流す（同じクリップを流している最中なら続ける）
        /// @param clipName クリップの名前（見つからなければ何もしない）
        void Play(const std::string& clipName);

        /// @brief 止める（コマはそのまま）
        void Stop();

        /// @brief 流しているクリップの名前（止めていても最後に流したもの）
        const std::string& GetCurrentClipName() const { return currentClip_; }

        /// @brief 流しているか（ループしないクリップが終わると false）
        bool IsPlaying() const { return playing_; }

        // ===== アニメーターから動かす =====

        /// @brief アニメーターに動かされているか（true の間は Start と Update で自分では進めない）
        void SetDriven(bool driven) { driven_ = driven; }
        bool IsDriven() const { return driven_; }

        /// @brief クリップの決まった時刻のコマを出す
        /// @param clipName クリップの名前（見つからなければ何もしない）
        /// @param seconds クリップの頭からの秒（尺を越えた分は最後のコマ）
        /// @return クリップが見つかれば true
        bool ShowClipAt(const std::string& clipName, float seconds);

        /// @brief クリップ 1 周の秒（fps が 0 以下なら 0）
        static float ClipDuration(const SpriteAnimationClip& clip);

        // ===== 設定 =====

        /// @brief 再生を始めたときに流すクリップ（空なら流さない）
        const std::string& GetDefaultClip() const { return defaultClip_; }
        void SetDefaultClip(const std::string& clipName) { defaultClip_ = clipName; }

        /// @brief 速さの倍率（1 で各クリップの fps どおり）
        float GetSpeed() const { return speed_; }
        void SetSpeed(float speed) { speed_ = speed; }

        /// @brief クリップの一覧
        const std::vector<SpriteAnimationClip>& GetClips() const { return clips_; }
        std::vector<SpriteAnimationClip>& GetClips() { return clips_; }

        /// @brief 名前でクリップを引く（無ければ nullptr）
        const SpriteAnimationClip* FindClip(const std::string& clipName) const;

        // ===== 保存（型記述子が呼ぶ） =====

        void SaveClipsToJson(json& parameters) const;
        void LoadClipsFromJson(const json& parameters);

    private:
        /// @brief 経過時間から、クリップの中の今のコマを決める
        /// @param wrap true ならコマの数で巻き戻し、false なら最後のコマで止める
        int ResolveFrame(const SpriteAnimationClip& clip, bool wrap) const;

        /// @brief 兄弟のスプライト描画
        SpriteRendererComponent* GetSprite() const;

        std::vector<SpriteAnimationClip> clips_;
        std::string defaultClip_;
        float speed_ = 1.0f;

        std::string currentClip_;
        float time_ = 0.0f;
        bool playing_ = false;
        bool driven_ = false;
    };
}
