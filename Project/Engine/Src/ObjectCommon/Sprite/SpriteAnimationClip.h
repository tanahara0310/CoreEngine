#pragma once

#include <vector>
#include <string>

namespace CoreEngine
{
    /// @brief アニメーションの1フレームデータ
    struct AnimationFrame {
        float       texLeft = 0.0f;  ///< テクスチャ上のピクセル X 座標
        float       texTop = 0.0f;  ///< テクスチャ上のピクセル Y 座標
        float       texWidth = 0.0f;  ///< フレームのピクセル幅（0 でテクスチャ全体）
        float       texHeight = 0.0f;  ///< フレームのピクセル高さ（0 でテクスチャ全体）
        float       duration = 0.0f;  ///< このフレームの表示時間（秒）
        std::string texturePath;        ///< フレームごとに切り替えるテクスチャパス（空文字で前フレームを継続）
    };

    /// @brief スプライトアニメーションのフレームデータを保持するクラス
    class SpriteAnimationClip
    {
    public:
        /// @brief フレームを1枚追加
        /// @param texLeft   テクスチャ上のピクセル X 座標
        /// @param texTop    テクスチャ上のピクセル Y 座標
        /// @param texWidth  フレームのピクセル幅
        /// @param texHeight フレームのピクセル高さ
        /// @param duration  このフレームの表示時間（秒）
        void AddFrame(float texLeft, float texTop, float texWidth, float texHeight, float duration);

        /// @brief 各フレームにテクスチャパスを指定してフレームを追加
        /// @param texturePath このフレームのテクスチャパス
        /// @param duration    表示時間（秒）
        void AddFrameWithTexture(const std::string& texturePath, float duration);

        /// @brief 複数のテクスチャパスリストからクリップを一括生成
        /// @param texturePaths フレーム順に並んだテクスチャパスのリスト
        /// @param fps　再生速度（フレーム毎秒）
        /// @param loop　ループ再生するか
        static SpriteAnimationClip CreateFromTextures(
            const std::vector<std::string>& texturePaths,
            float fps,
            bool  loop = true);

        /// @brief 均等グリッドのスプライトシートからクリップを一括生成
        /// @param frameWidth  1フレームのピクセル幅
        /// @param frameHeight 1フレームのピクセル高さ
        /// @param frameCount  フレーム総数
        /// @param columns     シート1行あたりのフレーム数
        /// @param fps         再生速度（フレーム毎秒）
        /// @param loop        ループ再生するか
        /// @param startX      シート上の開始ピクセル X
        /// @param startY      シート上の開始ピクセル Y
        /// @return 生成されたクリップ
        static SpriteAnimationClip CreateUniform(
            float frameWidth, float frameHeight,
            int   frameCount, int   columns,
            float fps,
            bool  loop = true,
            float startX = 0.0f,
            float startY = 0.0f);

        // ===== アクセッサ =====
        const std::vector<AnimationFrame>& GetFrames()     const { return frames_; }
        int GetFrameCount() const { return static_cast<int>(frames_.size()); }
        bool IsLoop() const { return loop_; }
        void SetLoop(bool loop) { loop_ = loop; }

    private:
        std::vector<AnimationFrame> frames_;
        bool loop_ = true;
    };

} // namespace CoreEngine
