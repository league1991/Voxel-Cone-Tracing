
#version 420

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in VertexData{
	vec3 pos;
	vec3 normal;
	vec2 uv;
} In[3];

out VoxelData{
	vec3 posTexSpace;
	vec3 normal;
	vec2 uv;
} Out;

uniform uint voxelTexSize;
uniform mat4 voxelGridTransform;
uniform mat4 voxelGridTransformI;

uniform vec3 voxelGridSize;  // The dimensions in worlspace that make up the whole voxel-volume e.g. vec3(50,50,50);
uniform mat4 viewProjs[3];
//uniform vec3 worldAxes[3];

// Constants to key into worldaxes
const uint X = 0;
const uint Y = 1;
const uint Z = 2;


uint calcProjAxis() {
	const vec3 worldAxes[3] = vec3[3](vec3(1.0, 0.0, 0.0),
		vec3(0.0, 1.0, 0.0),
		vec3(0.0, 0.0, 1.0));

	// Determine world-axis along wich the projected triangle-area is maximized
	uint projAxis = 0;
	float maxArea = 0.0;
	for (uint i = 0; i < 3; ++i) {
		// Assume we work with per-triangle normals, so that each vertex-normal of
		// one triangle is equal.
		float area = abs(dot(In[0].normal, worldAxes[i]));
		if (area > maxArea) {
			maxArea = area;
			projAxis = i;
		}
	}

	return projAxis;
}

void main()
{
	const vec3 worldAxes[3] = vec3[3](vec3(1.0, 0.0, 0.0),
		vec3(0.0, 1.0, 0.0),
		vec3(0.0, 0.0, 1.0));

	vec3 voxelSize = voxelGridSize / vec3(voxelTexSize);

	uint projAxisIdx = calcProjAxis();
	vec3 middle = (viewProjs[projAxisIdx] * vec4((In[0].pos + In[1].pos + In[2].pos) / 3.0, 1.0)).xyz;
	for (int i = 0; i < gl_in.length(); i++) {
		vec3 projPos = (viewProjs[projAxisIdx] * vec4(In[i].pos, 1.0)).xyz;
		// Approximate conservative rasterization
		//projPos += normalize(projPos - middle) * (voxelSize.x / 2.0);

		gl_Position = vec4(projPos, 1.0);

		Out.posTexSpace = (voxelGridTransformI * vec4(In[i].pos, 1.0)).xyz * 0.5 + 0.5;
		//Out.posTexSpace = (inverse(voxelGridTransform) * vec4(In[i].pos, 1.0)).xyz * 0.5 + 0.5;
		Out.normal = In[i].normal;
		Out.uv = In[i].uv;

		// done with the vertex
		EmitVertex();
	}
	EndPrimitive();
}