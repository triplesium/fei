namespace pbr.color {

public float3 srgb_to_linear(float3 value) {
    return pow(max(value, float3(0.0)), float3(2.2));
}

public float3 linear_to_srgb(float3 value) {
    return pow(max(value, float3(0.0)), float3(1.0 / 2.2));
}

public float3 reinhard_tonemap(float3 color) {
    return color / (color + float3(1.0));
}

public float3 tonemap_reinhard_to_srgb(float3 color) {
    return linear_to_srgb(reinhard_tonemap(color));
}

} // namespace pbr.color
