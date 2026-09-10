#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief シーン側から雲のオン/オフを操作するための取っ手
    /// @details 登録は `AddFeature(CreateSkyFogFeature())` の 1 行のままでよい。
    ///          あとから触りたくなったら `GetFeature<GameComponents::ISkyFogFeature>()`
    ///          で引ける（BaseScene の protected メンバーなのでシーンから直接呼べる）。
    /// @code
    ///     // 登録時に決める（このシーンでは最初から出さない）
    ///     AddFeature(GameComponents::CreateSkyFogFeature(false));
    ///
    ///     // 好きなタイミングで切り替える（OnInitialize / OnUpdate のどちらからでも）
    ///     if (auto* fog = GetFeature<GameComponents::ISkyFogFeature>()) {
    ///         fog->SetEnabled(false);
    ///     }
    /// @endcode
    class ISkyFogFeature : public CoreEngine::ISceneFeature {
    public:
        /// @brief このシーンで雲を出すかを切り替える
        /// @details 切った時点でシーン開始時のフォグ設定へ戻る（次のフレームを待たない）。
        /// @note 画に出るのは、ここと CVar `Game.Fog.Enabled` の両方が true のとき。
        ///       CVar は「ゲーム設定」からの全体スイッチで、こちらがシーンごとの指定。
        virtual void SetEnabled(bool enabled) = 0;

        /// @brief シーン側の指定を返す（CVar `Game.Fog.Enabled` は見ない）
        virtual bool IsEnabled() const = 0;
    };

    /// @brief ステージのブロックより下を雲で埋める Feature を作る
    /// @param enabled 登録直後に雲を出すか。false なら SetEnabled(true) まで出さない
    /// @details GameScene・ResultScene の OnInitialize() から
    ///          `AddFeature(CreateSkyFogFeature())` で登録する。
    ///          シーンにいる間だけエンジンの高さフォグ（r.Fog.*）を雲の設定に差し替え、
    ///          シーンを抜けるときに元へ戻す（タイトルへ持ち出さないため）。
    /// @note 雲の明るさは太陽高度に追従して夜に落ちる（`Game.Fog.NightBrightnessEV`）。
    ///       フォグ色は時刻に追従しない絶対値なので、そのままだと夜の自動露出に
    ///       持ち上げられて雲が白飛びし、ステージまで霞んで見える。
    /// @note 調整値は SkyFogFeature.cpp のファイルスコープにある `Game.Fog.*` の CVar 群。
    ///       CVars.json へ自動保存され、インスペクターの「ゲーム設定」から編集できる。
    std::unique_ptr<ISkyFogFeature> CreateSkyFogFeature(bool enabled = true);

    /// @brief 突入演出のあいだだけ、雲を持ち上げて「雲の中」を作る
    /// @param lift 0 = CVar のとおり（通常）／1 = 下の 2 つで指定した開幕の雲。間は補間する
    /// @param baseHeight lift = 1 のときの雲のいちばん濃い高さ [m]
    /// @param heightFalloff lift = 1 のときの高さ減衰（小さいほど厚くぼんやりする）
    ///
    /// @details **CVar は一切書き換えない。** ここが肝心で、`Game.Fog.*` を毎フレーム
    ///          書き換えると CVar の自動保存が演出の途中で走り、掃引中の値
    ///          （雲の高さ 56m など）が CVars.json へ焼き付く。そうなると次回起動から
    ///          タイトル画面が雲の中＝真っ白で始まる。実際にそれを踏んだので、
    ///          演出用の値は保存されないここへ置いてある。
    ///
    /// @note シーンの出入りで自動的に 0 へ戻る。Feature が登録されていないシーンでは
    ///       値を覚えるだけで何も起きない。駆動するのは GameEntranceFeature。
    void SetSkyFogCloudLift(float lift, float baseHeight, float heightFalloff);
}
