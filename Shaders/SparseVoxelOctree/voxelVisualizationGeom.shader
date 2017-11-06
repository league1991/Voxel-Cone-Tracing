// Author:	Fredrik Präntare <prantare@gmail.com>
// Date:	11/26/2016
#version 450 core

layout(points) in;
layout(triangle_strip, max_vertices = 36) out;

layout(rgba8) uniform image3D brickPool_color;

uniform mat4 voxelGridTransformG;
uniform uint levelG;
uniform uint numLevels;  // Number of levels in the octree
const float levelTexSizeG[] = {1.0,       1 / 2.0,  1 / 4.0,  1 / 8.0,
                              1 / 16.0,  1 / 32.0, 1 / 64.0, 1 / 128.0,
                              1 / 256.0, 1 / 512.0};

in VertexData{
	vec4 posWorldSpace;
	//vec4 color;
	ivec3 brickAddress;
} In[1];

out VoxelData{
	vec4 pos;
	vec4 color;
} Out;

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;

const vec3 offset[8] = vec3[8](
	vec3(0.0, 0.0, 0.0),	vec3(1.0, 0.0, 0.0),
	vec3(1.0, 1.0, 0.0),	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0),	vec3(1.0, 0.0, 1.0),
	vec3(1.0, 1.0, 1.0),	vec3(0.0, 1.0, 1.0)
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

	//Out.color = In[0].color;
	vec4 posWorldSpace = In[0].posWorldSpace;
	ivec3 brickAddress = In[0].brickAddress;

	float deltaTex = levelTexSizeG[levelG];
	vec4 deltaWorld = voxelGridTransformG * vec4(deltaTex, deltaTex, deltaTex, 0.0) * 0.9;
	bool validAddress = brickAddress.r >= 0;

	float alphaFactor = 0.4 / pow(8.0, float(numLevels) - float(levelG));
	Out.color = vec4(1, 0, 0, alphaFactor);

	//vec4 clr = vec4(0, 0, 0, 0);
	//for (int x = 0; x < 3; ++x) {
	//	for (int y = 0; y < 3; ++y) {
	//		for (int z = 0; z < 3; ++z) {
	//			clr += imageLoad(brickPool_color, brickAddress + ivec3(x,y,z));
	//		}
	//	}
	//}
	//Out.color = clr / 27;

	for (int i = 0; i < 12; ++i)
	{
		uvec3 outIdx = triIndex[i];
		vec4 pos0 = posWorldSpace + vec4(offset[outIdx.x], 0.0) * deltaWorld;
		gl_Position = viewProjTransform * pos0;
		if (validAddress) {
			Out.color = imageLoad(brickPool_color, brickAddress + ivec3(offset[outIdx.x] * 2.0));
			Out.color.a = Out.color.a * alphaFactor + 1.0/255.0;
		}
		EmitVertex();

		vec4 pos1 = posWorldSpace + vec4(offset[outIdx.y], 0.0) * deltaWorld;
		gl_Position = viewProjTransform * pos1;
		if (validAddress) {
			Out.color = imageLoad(brickPool_color, brickAddress + ivec3(offset[outIdx.y] * 2.0));
			Out.color.a = Out.color.a * alphaFactor + 1.0 / 255.0;
		}
		EmitVertex();

		vec4 pos2 = posWorldSpace + vec4(offset[outIdx.z], 0.0) * deltaWorld;
		gl_Position = viewProjTransform * pos2;
		if (validAddress){
			Out.color = imageLoad(brickPool_color, brickAddress + ivec3(offset[outIdx.z] * 2.0));
			Out.color.a = Out.color.a * alphaFactor + 1.0 / 255.0;
		}
		EmitVertex();

		EndPrimitive();
	}
}