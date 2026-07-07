namespace pbr.vxgi {

public struct OrthonormalBasis {
    public float3 right;
    public float3 up;

    public static OrthonormalBasis from_normal(float3 normal) {
        float3 guide = float3(0.0, 1.0, 0.0);
        if (abs(dot(normal, guide)) == 1.0) {
            guide = float3(0.0, 0.0, 1.0);
        }

        OrthonormalBasis basis;
        basis.right = normalize(guide - dot(normal, guide) * normal);
        basis.up = cross(basis.right, normal);
        return basis;
    }

    public float3 cone_direction(float3 normal, float3 local_direction) {
        float3 direction = normal;
        direction += local_direction.x * right + local_direction.z * up;
        return normalize(direction);
    }
};

public OrthonormalBasis basis_from_normal(float3 normal) {
    return OrthonormalBasis.from_normal(normal);
}

public float3 cone_direction_from_basis(
    float3 normal,
    OrthonormalBasis basis,
    float3 local_direction
) {
    return basis.cone_direction(normal, local_direction);
}

public uint3 visible_aniso_faces(float3 direction) {
    uint3 visible_face;
    visible_face.x = direction.x < 0.0 ? 0 : 1;
    visible_face.y = direction.y < 0.0 ? 2 : 3;
    visible_face.z = direction.z < 0.0 ? 4 : 5;
    return visible_face;
}

} // namespace pbr.vxgi
