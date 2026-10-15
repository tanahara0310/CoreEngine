// プールから貸し出す 1 つのモデル。最後に書いた値を覚えていて、変わったときだけエンジンへ書く
class PooledModel
{
    GameObject@ object;
    Transform@ transform;
    Material@ material;
    private Vector3 position_;
    private Vector3 rotation_;
    private Vector3 scale_;
    private Vector4 color_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    private bool placed_ = false;

    PooledModel(GameObject@ instance)
    {
        @object = instance;
        @transform = instance.transform;
        @material = instance.material;
    }

    void Place(const Vector3 &in position, const Vector3 &in rotation, const Vector3 &in scale)
    {
        if (!placed_ || !(position == position_)) {
            transform.position = position;
            position_ = position;
        }
        if (!placed_ || !(rotation == rotation_)) {
            transform.rotation = rotation;
            rotation_ = rotation;
        }
        if (!placed_ || !(scale == scale_)) {
            transform.scale = scale;
            scale_ = scale;
        }
        placed_ = true;
    }

    // 色は 1/512 より小さい変化を書かない（毎フレーム少しずつ変わる色ムラのため）
    void SetColor(const Vector4 &in color)
    {
        if (material is null || !material.exists) {
            return;
        }
        if (abs(color.x - color_.x) < 0.002f && abs(color.y - color_.y) < 0.002f
            && abs(color.z - color_.z) < 0.002f && abs(color.w - color_.w) < 0.002f) {
            return;
        }
        material.color = color;
        color_ = color;
    }

    void Forget()
    {
        placed_ = false;
    }
}

// 同じプレハブから作ったオブジェクトを貸し出して使い回す。返したものは見えなくする
class ModelPool
{
    private GameObject@ owner_;
    private string prefab_;
    private string namePrefix_;
    private array<PooledModel@> free_;
    private int created_ = 0;

    ModelPool(GameObject@ owner, const string &in prefab, const string &in namePrefix)
    {
        @owner_ = owner;
        prefab_ = prefab;
        namePrefix_ = namePrefix;
    }

    PooledModel@ Acquire()
    {
        PooledModel@ model;
        if (free_.length() > 0) {
            @model = free_[free_.length() - 1];
            free_.removeLast();
        } else {
            GameObject@ instance = owner_.InstantiatePrefab(prefab_, namePrefix_ + created_);
            if (instance is null) {
                return null;
            }
            ++created_;
            @model = PooledModel(instance);
        }
        model.Forget();
        model.object.active = true;
        return model;
    }

    void Release(PooledModel@ model)
    {
        if (model is null) {
            return;
        }
        model.object.active = false;
        free_.insertLast(model);
    }

    // 先に作って見えなくしておく
    void Prewarm(int count)
    {
        array<PooledModel@> taken;
        for (int i = 0; i < count; ++i) {
            PooledModel@ model = Acquire();
            if (model is null) {
                break;
            }
            taken.insertLast(model);
        }
        for (uint i = 0; i < taken.length(); ++i) {
            Release(taken[i]);
        }
    }

    int GetCreatedCount() const
    {
        return created_;
    }
}
