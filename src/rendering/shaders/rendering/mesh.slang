public float3x3 inverse_transpose(float3x3 m) {
    float a = m[0][0];
    float b = m[0][1];
    float c = m[0][2];
    float d = m[1][0];
    float e = m[1][1];
    float f = m[1][2];
    float g = m[2][0];
    float h = m[2][1];
    float i = m[2][2];

    float3 row0 = float3(e * i - f * h, f * g - d * i, d * h - e * g);
    float3 row1 = float3(c * h - b * i, a * i - c * g, b * g - a * h);
    float3 row2 = float3(b * f - c * e, c * d - a * f, a * e - b * d);
    float determinant = a * row0.x + b * row0.y + c * row0.z;
    return float3x3(row0 / determinant, row1 / determinant, row2 / determinant);
}

public struct MeshUniform {
    public float4x4 world_from_local;

    public float4 position_ws4(float3 position_local) {
        return mul(world_from_local, float4(position_local, 1.0));
    }

    public float3 position_ws(float3 position_local) {
        return position_ws4(position_local).xyz;
    }

    public property float3x3 normal_matrix {
        get {
            return inverse_transpose((float3x3)world_from_local);
        }
    }

    public float3 normal_ws(float3 normal_local) {
        return mul(normal_matrix, normal_local);
    }

    public float3 tangent_ws(float3 tangent_local) {
        return mul(normal_matrix, tangent_local);
    }
};
