using namespace CoreEngine;

// シーンをまたぐ値と、シーンの切り替え（元の SceneManager と StageConfig）

const string kSceneTitle = "Title";
const string kSceneStageSelect = "StageSelect";
const string kSceneGame = "Game";

const int kMaxStage = 13;
const string kStageSessionKey = "Reprism.Stage";

// 今のステージ番号（1 から）
int GetStageNumber()
{
    return Session::GetInt(kStageSessionKey, 1);
}

void SetStageNumber(int stage)
{
    Session::SetInt(kStageSessionKey, stage);
}

// フェード（Project.json の Fade）のマテリアル。ブロックに割れて落ちる形にして揺れの時間を今から測る
ShaderMaterial@ PrepareFadeFall()
{
    ShaderMaterial@ fade = PostEffect::GetMaterial("Fade");
    if (fade !is null) {
        fade.SetFloat("fadeType", 9.0f);
        fade.SetFloat("timeOrigin", Time::TimeSinceStartup());
    }
    return fade;
}

// 元の ChangeScene(name)：ブロックに割れて落ちて（1.4 秒）、新しいシーンで戻る（1.0 秒）
void ChangeSceneTd3(const string &in name)
{
    PrepareFadeFall();
    Scene::ChangeScene(name);
}

// 元の ChangeScene(name, FadeInOnly)：落としきった画面から、新しいシーンで戻るだけ
void ChangeSceneFadeInOnly(const string &in name)
{
    SceneTransition transition = Scene::GetDefaultTransition();
    transition.skipOut = true;
    Scene::ChangeScene(name, transition);
}

// ゲームの BGM はステージを切り替えても鳴らし続ける（元の gGameSceneBgm）
Sound@ gGameSceneBgm;
bool gPreserveGameSceneBgm = false;

void EnsureGameSceneBgm()
{
    if (gGameSceneBgm !is null && gGameSceneBgm.isValid) {
        return;
    }
    PlayParams params;
    params.bus = AudioBus::BGM;
    params.loop = true;
    @gGameSceneBgm = Audio::PlayScoped("Application/Assets/Audio/Bgm/gameScene.mp3", params);
}

void StopGameSceneBgm()
{
    if (gGameSceneBgm is null) {
        return;
    }
    gGameSceneBgm.Stop();
    @gGameSceneBgm = null;
}

// 効果音を 1 回鳴らす（元の SoundResource::Play(false)）
void PlaySe(const string &in fileName)
{
    PlayParams params;
    params.bus = AudioBus::SE;
    Audio::PlayOneShot("Application/Assets/Audio/Se/" + fileName, params);
}
