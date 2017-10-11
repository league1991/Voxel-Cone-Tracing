// Author:	Fredrik Präntare <prantare@gmail.com>
// Date:	11/26/2016
#version 450 core

layout(points) in;
layout(triangle_strip, max_vertices = 36) out;

in VertexData{
	vec4 posWorldSpace;
	vec4 color;
} In[1];

out VoxelData{
	vec4 pos;
	vec4 color;
} Out;

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;

const vec3 offset[8] = vec3[8](
	vec3(-1.0, -1.0, -1.0),	vec3(1.0, -1.0, -1.0),
	vec3(1.0, 1.0, -1.0),	vec3(-1.0, 1.0, -1.0),
	vec3(-1.0, -1.0, 1.0),	vec3(1.0, -1.0, 1.0),
	vec3(1.0, 1.0, 1.0),	vec3(-1.0, 1.0, 1.0)
	);

const uvec3 triIndex[12] = uvec3[12](
	uvec3(0, 3, 1),	uvec3(3, 2, 1),
	uvec3(0, 1, 5),	uvec3(0, 5, 4),
	uvec3(1, 6, 5),	uvec3(1, 2, 6),
	uvec3(2, 3, 7),	uvec3(2, 7, 6),
	uvec3(3, 0, 4),	uvec3(3, 4, 7),
	uvec3(4, 5, 7),	uvec3(5, 6, 7));

void main(){
	mat4 viewProjTransform = P * V;

	Out.color = In[0].color;
	vec4 posWorldSpace = In[0].posWorldSpace;

	for (int i = 0; i < 12; ++i)
	{
		uvec3 outIdx = triIndex[i];
		vec4 pos0 = posWorldSpace + vec4(offset[outIdx.x] * 0.01, 0.0);
		gl_Position = viewProjTransform * pos0;
		EmitVertex();

		vec4 pos1 = posWorldSpace + vec4(offset[outIdx.y] * 0.01, 0.0);
		gl_Position = viewProjTransform * pos1;
		EmitVertex();

		vec4 pos2 = posWorldSpace + vec4(offset[outIdx.z] * 0.01, 0.0);
		gl_Position = viewProjTransform * pos2;
		EmitVertex();

		EndPrimitive();
	}
}