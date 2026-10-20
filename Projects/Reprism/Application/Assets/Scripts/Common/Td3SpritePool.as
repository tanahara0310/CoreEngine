using namespace CoreEngine;

// 粒の板の使い回し（使い終わった板は隠して次に回す）
class Td3SpritePool
{
    private Td3SpriteFactory@ factory_;
    private string texture_;
    private string name_;
    private array<Td3Sprite@> free_;

    Td3SpritePool(Td3SpriteFactory@ factory, const string &in texture, const string &in name)
    {
        @factory_ = factory;
        texture_ = texture;
        name_ = name;
    }

    Td3Sprite@ Acquire()
    {
        if (free_.length() > 0) {
            Td3Sprite@ sprite = free_[free_.length() - 1];
            free_.removeLast();
            sprite.SetActive(true);
            return sprite;
        }
        return factory_.Create(texture_, name_);
    }

    void Release(Td3Sprite@ sprite)
    {
        if (sprite is null) {
            return;
        }
        sprite.SetActive(false);
        free_.insertLast(sprite);
    }
}
