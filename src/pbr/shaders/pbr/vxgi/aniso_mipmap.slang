namespace pbr.vxgi.aniso_mipmap {

public static const int3 OFFSETS[8] = {
    int3(1, 1, 1),
    int3(1, 1, 0),
    int3(1, 0, 1),
    int3(1, 0, 0),
    int3(0, 1, 1),
    int3(0, 1, 0),
    int3(0, 0, 1),
    int3(0, 0, 0),
};

public float4 over(float4 front, float4 back) {
    return front + back * (1.0 - front.a);
}

public void compose_faces(float4 values[8], inout float4 faces[6]) {
    faces[0] =
        (over(values[0], values[4]) +
         over(values[1], values[5]) +
         over(values[2], values[6]) +
         over(values[3], values[7])) *
        0.25;
    faces[1] =
        (over(values[4], values[0]) +
         over(values[5], values[1]) +
         over(values[6], values[2]) +
         over(values[7], values[3])) *
        0.25;
    faces[2] =
        (over(values[0], values[2]) +
         over(values[1], values[3]) +
         over(values[5], values[7]) +
         over(values[4], values[6])) *
        0.25;
    faces[3] =
        (over(values[2], values[0]) +
         over(values[3], values[1]) +
         over(values[7], values[5]) +
         over(values[6], values[4])) *
        0.25;
    faces[4] =
        (over(values[0], values[1]) +
         over(values[2], values[3]) +
         over(values[4], values[5]) +
         over(values[6], values[7])) *
        0.25;
    faces[5] =
        (over(values[1], values[0]) +
         over(values[3], values[2]) +
         over(values[5], values[4]) +
         over(values[7], values[6])) *
        0.25;
}

} // namespace pbr.vxgi.aniso_mipmap
