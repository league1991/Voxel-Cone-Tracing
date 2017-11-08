// Author:	Fredrik Präntare <prantare@gmail.com>
// Date:	11/26/2016
#version 450 core
//#define THREAD_MODE 0
//
uniform usamplerBuffer levelAddressBuffer;
uniform uint level;
//
#include "SparseVoxelOctree/_utilityFunctions.shader"
#include "SparseVoxelOctree/_traverseUtil.shader"

layout(r32ui) uniform uimageBuffer nodePool_next;
layout(r32ui) uniform uimageBuffer nodePool_color;
layout(r32ui) uniform uimageBuffer voxelFragList_position;
layout(r32ui) uniform uimage3D voxelFragTex_color;

uniform uint voxelTexSize;
uniform mat4 voxelGridTransform;

const float levelTexSize[] = {1.0,       1 / 2.0,  1 / 4.0,  1 / 8.0,
                              1 / 16.0,  1 / 32.0, 1 / 64.0, 1 / 128.0,
                              1 / 256.0, 1 / 512.0};

out VertexData{
	vec4 posWorldSpace;
	//vec4 color;
	ivec3 brickAddress;
} Out;

int traverseToLevel(in vec3 posTex, out uint foundOnLevel) {
	vec3 nodePosTex = vec3(0.0);
	vec3 nodePosMaxTex = vec3(1.0);
	int nodeAddress = 0;
	foundOnLevel = 0;
	float sideLength = 0.5;

	for (foundOnLevel = 0; foundOnLevel < level; ++foundOnLevel) {
		uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
		uint childStartAddress = nodeNext & NODE_MASK_VALUE;
		if (childStartAddress == 0U) {
			break;
		}

		uvec3 offVec = uvec3(2.0 * posTex);
		uint off = offVec.x + 2U * offVec.y + 4U * offVec.z;

		// Restart while-loop with the child node (aka recursion)
		nodeAddress = int(childStartAddress + off);
		nodePosTex += vec3(childOffsets[off]) * vec3(sideLength);
		nodePosMaxTex = nodePosTex + vec3(sideLength);

		sideLength = sideLength / 2.0;
		posTex = 2.0 * posTex - vec3(offVec);
	} // level-for

	return nodeAddress;
}

void main(){
	// Get position
	uvec4 positionU = imageLoad(voxelFragList_position, int(gl_VertexID));
	uvec3 baseVoxel = uintXYZ10ToVec3(positionU.x);
	vec3 posTexSpace = vec3(baseVoxel) / float(voxelTexSize);
	uvec3 posTexSpacei = uvec3(posTexSpace / levelTexSize[level]);
	posTexSpace = vec3(posTexSpacei) * levelTexSize[level];

	Out.posWorldSpace = voxelGridTransform * vec4(posTexSpace, 1.0);


	//uvec4 colorU = imageLoad(voxelFragTex_color, ivec3(baseVoxel));
	//Out.color = convRGBA8ToVec4(colorU.x) / 255.0;

	uint onLevel = 0;
	int nodeAddress = traverseToLevel(posTexSpace, onLevel);
	//Out.brickAddress = ivec3(uintXYZ10ToVec3(imageLoad(nodePool_color, int(nodeAddress)).x));
	if (onLevel == level) {
		Out.brickAddress = ivec3(uintXYZ10ToVec3(imageLoad(nodePool_color, int(nodeAddress)).x));
		//vec4 brickColor = imageLoad(brickPool_color, brickAddress);
		//Out.color = brickColor;
	}
	else {
		//Out.color = vec4(1, 0, 0, 0.2);
		Out.brickAddress = ivec3(-1, -1, -1);
	}
}