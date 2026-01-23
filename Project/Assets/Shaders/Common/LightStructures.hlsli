/// @brief ディレクショナルライトの構造体
struct DirectionalLightData
{
    float4 color;        // ライトの色
    float3 direction;    // ライトの方向
    float intensity;     // 輝度
    bool enabled;        // 有効フラグ
    float3 padding;      // パディング
};

/// @brief ポイントライトの構造体
struct PointLightData
{
    float4 color;        // 光源の色
    float3 position;     // 光源の位置
    float intensity;     // 光源の強度
    float radius;        // ライトの届く最大距離
    float decay;         // 光の減衰率
    bool enabled;        // 有効フラグ
    float padding;       // パディング
};

/// @brief スポットライトの構造体
struct SpotLightData
{
    float4 color;            // 光源の色
    float3 position;         // 光源の位置
    float intensity;         // 光源の強度
    float3 direction;        // 光源の向き
    float distance;          // スポットライトの届く距離
    float decay;             // 光の減衰率
    float cosAngle;          // スポットライトの角度
    float cosFalloffStart;   // スポットライトの角度の開始位置
    bool enabled;            // 有効フラグ
};

/// @brief エリアライト（矩形）の構造体
struct AreaLightData
{
    float4 color;            // 光源の色
    float3 position;         // 光源の中心位置
    float intensity;         // 光源の強度
    float3 direction;        // ライトの向き（法線方向）
    float width;             // 矩形の幅
    float3 right;            // 矩形の右方向ベクトル
    float height;            // 矩形の高さ
    float3 up;               // 矩形の上方向ベクトル
    float decay;             // 光の減衰率
    bool enabled;            // 有効フラグ
    float3 padding;          // パディング
};

/// @brief ライトカウント用の構造体
struct LightCounts
{
    uint directionalLightCount;
    uint pointLightCount;
    uint spotLightCount;
    uint areaLightCount;
};
