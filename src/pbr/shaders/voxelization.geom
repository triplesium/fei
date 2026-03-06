#version 430

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in Vertex
{
    vec2 texCoord;
    vec3 normal;
} In[3];

out GeometryOut
{
	vec3 wsPosition;
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    flat vec4 triangleAABB;
} Out;

layout(row_major, std140) uniform VxgiVoxelization {
    mat4 view_projections[3];
    mat4 inv_view_projections[3];
    uint volume_dimension;
    uint flag_static_voxels;
    float voxel_scale;
    float voxel_size;
    vec3 world_min_point;
};

int CalculateAxis()
{
	vec3 p1 = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
	vec3 p2 = gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz;
	vec3 faceNormal = cross(p1, p2);

	float nDX = abs(faceNormal.x);
	float nDY = abs(faceNormal.y);
	float nDZ = abs(faceNormal.z);

	if( nDX > nDY && nDX > nDZ )
    {
		return 0;
	}
	else if( nDY > nDX && nDY > nDZ  )
    {
	    return 1;
    }
	else
    {
	    return 2;
	}
}

vec4 AxisAlignedBoundingBox(vec4 pos[3], vec2 pixelDiagonal)
{
	vec4 aabb;

	aabb.xy = min(pos[2].xy, min(pos[1].xy, pos[0].xy));
	aabb.zw = max(pos[2].xy, max(pos[1].xy, pos[0].xy));

	// enlarge by half-pixel
	aabb.xy -= pixelDiagonal;
	aabb.zw += pixelDiagonal;

	return aabb;
}

void main()
{
	vec2 texCoord[3];
	int selectedIndex = CalculateAxis();
	mat4 viewProjection = view_projections[selectedIndex];
	mat4 viewProjectionI = inv_view_projections[selectedIndex];

    for (int i = 0; i < gl_in.length(); i++)
    {
        texCoord[i] = In[i].texCoord; 
    }

	//transform vertices to clip space
	vec4 pos[3] = vec4[3]
	(
		viewProjection * gl_in[0].gl_Position,
		viewProjection * gl_in[1].gl_Position,
		viewProjection * gl_in[2].gl_Position
	);

	vec4 trianglePlane;
	trianglePlane.xyz = cross(pos[1].xyz - pos[0].xyz, pos[2].xyz - pos[0].xyz);
	trianglePlane.xyz = normalize(trianglePlane.xyz);
	trianglePlane.w = -dot(pos[0].xyz, trianglePlane.xyz);

    int indices[3] = {0, 1, 2};

    // change winding, otherwise there are artifacts for the back faces.
    if (dot(trianglePlane.xyz, vec3(0.0, 0.0, 1.0)) < 0.0)
    {
        vec4 vertexTemp = pos[2];
        vec2 texCoordTemp = texCoord[2];
        int indexTemp = indices[2];
        
        pos[2] = pos[1];
        texCoord[2] = texCoord[1];
        indices[2] = indices[1];
    
        pos[1] = vertexTemp;
        texCoord[1] = texCoordTemp;
        indices[1] = indexTemp;
    }

	vec2 halfPixel = vec2(1.0f / volume_dimension);

	if(trianglePlane.z == 0.0f) return;
	// expanded aabb for triangle
	Out.triangleAABB = AxisAlignedBoundingBox(pos, halfPixel);
	// calculate the plane through each edge of the triangle
	// in normal form for dilatation of the triangle
	vec3 planes[3];
	planes[0] = cross(pos[0].xyw - pos[2].xyw, pos[2].xyw);
	planes[1] = cross(pos[1].xyw - pos[0].xyw, pos[0].xyw);
	planes[2] = cross(pos[2].xyw - pos[1].xyw, pos[1].xyw);
	planes[0].z -= dot(halfPixel, abs(planes[0].xy));
	planes[1].z -= dot(halfPixel, abs(planes[1].xy));
	planes[2].z -= dot(halfPixel, abs(planes[2].xy));
	// calculate intersection between translated planes
	vec3 intersection[3];
	intersection[0] = cross(planes[0], planes[1]);
	intersection[1] = cross(planes[1], planes[2]);
	intersection[2] = cross(planes[2], planes[0]);
	intersection[0] /= intersection[0].z;
	intersection[1] /= intersection[1].z;
	intersection[2] /= intersection[2].z;
	// calculate dilated triangle vertices
	float z[3];
	z[0] = -(intersection[0].x * trianglePlane.x + intersection[0].y * trianglePlane.y + trianglePlane.w) / trianglePlane.z;
	z[1] = -(intersection[1].x * trianglePlane.x + intersection[1].y * trianglePlane.y + trianglePlane.w) / trianglePlane.z;
	z[2] = -(intersection[2].x * trianglePlane.x + intersection[2].y * trianglePlane.y + trianglePlane.w) / trianglePlane.z;
	pos[0].xyz = vec3(intersection[0].xy, z[0]);
	pos[1].xyz = vec3(intersection[1].xy, z[1]);
	pos[2].xyz = vec3(intersection[2].xy, z[2]);

	for (int i = 0; i < 3; i++)
	{
		pos[i].xy = clamp(pos[i].xy, Out.triangleAABB.xy, Out.triangleAABB.zw);
	}

	for(int i = 0; i < 3; ++i)
	{
		vec4 voxelPos = gl_in[indices[i]].gl_Position;
		voxelPos.xyz -= world_min_point;
		voxelPos *= voxel_scale;

		gl_Position = pos[i];
		Out.position = pos[i].xyz;
		Out.normal = In[indices[i]].normal;
		Out.texCoord = texCoord[i];
		Out.wsPosition = voxelPos.xyz * volume_dimension;

		EmitVertex();
	}

	EndPrimitive();
}
