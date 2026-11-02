#pragma once

namespace CoreEngine::WaterSettingsMigration
{
    /// @brief プロジェクトの CVars.json にある水の性質と風を、各シーンの水面と環境の設定へ移す
    /// @details 水の性質（`r.Water.*` の見た目・水質・波・うねり・泡・屈折率）はシーンとプレハブの水面コンポーネントへ、
    ///          風（`r.Water.FFT.WindSpeed`・`r.Water.FFT.WindDirection`）は各シーンの `_environment.json` の `r.Wind.*` へ書き、
    ///          CVars.json からは消す。すでに値を持っている水面とシーンは書き換えない。
    ///          移す値が無ければ何もしない。移す前の CVars.json は `Saved/EditorSettings` に控えを残す。
    /// @note CVar をプロジェクト設定から読み込むより前に呼ぶ。
    void MigrateProject();
}
