// Author:	Fredrik Präntare <prantare@gmail.com>
// Date:	11/26/2016
#version 450 core

layout(r32ui) uniform uimageBuffer voxelFragList_position;
layout(r32ui) uniform uimage3D voxelFragTex_color;

uniform uint voxelTexSize;
uniform mat4 voxelGridTransform;

out VertexData{
	vec4 posWorldSpace;
	vec4 color;
} Out;

uint vec3ToUintXYZ10(uvec3 val) {
	return (uint(val.z) & 0x000003FF) << 20U
		 | (uint(val.y) & 0x000003FF) << 10U
		 | (uint(val.x) & 0x000003FF);
}

uvec3 uintXYZ10ToVec3(uint v)
{
	uint z = (v >> 20U) & 0x000003FF;
	uint y = (v >> 10U) & 0x000003FF;
	uint x = (v >> 0U) & 0x000003FF;
	return uvec3(x, y, z);
}

vec4 convRGBA8ToVec4(uint val) {
	return vec4(float((val & 0x000000FF)),
		float((val & 0x0000FF00) >> 8U),
		float((val & 0x00FF0000) >> 16U),
		float((val & 0xFF000000) >> 24U));
}

void main(){
	// Get position
	uvec4 positionU = imageLoad(voxelFragList_position, int(gl_VertexID));
	uvec3 baseVoxel = uintXYZ10ToVec3(positionU.x);
	vec3 posTexSpace = vec3(baseVoxel) / float(voxelTexSize);
	//Out.posWorldSpace = inverse(voxelGridTransform) * vec4((posTexSpace - 0.5) * 2, 1.0);
	Out.posWorldSpace = voxelGridTransform * vec4(posTexSpace, 1.0);

	// Get Color
	uvec4 colorU = imageLoad(voxelFragTex_color, ivec3(baseVoxel));
	Out.color =  convRGBA8ToVec4(colorU.x) / 255.0;
}