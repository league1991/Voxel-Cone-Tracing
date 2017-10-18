

/** 
This shader writes the voxel-colors from the voxel fragment list into the
leaf nodes of the octree. The shader is lauched with one thread per entry of
the voxel fragment list.
Every voxel is read from the voxel fragment list and its position is used
to traverse the octree and find the leaf-node.
*/

#version 420 core

layout(r32ui) uniform uimageBuffer voxelFragList_position;
layout(r32ui) uniform uimage3D voxelFragTex_color;
layout(r32ui) uniform uimage3D voxelFragTex_normal;

layout(r32ui) uniform uimageBuffer nodePool_next;
layout(r32ui) uniform uimageBuffer nodePool_color;
layout(rgba8) uniform image3D brickPool_color;
layout(rgba8) uniform image3D brickPool_normal;
layout(rgba8) uniform image3D brickPool_irradiance;

uniform uint numLevels;  // Number of levels in the octree
uniform uint voxelGridResolution;

#include "SparseVoxelOctree/_utilityFunctions.shader"
#include "SparseVoxelOctree/_traverseUtil.shader"
#include "SparseVoxelOctree/_octreeTraverse.shader"

void storeInLeaf(in vec3 posTex, in int nodeAddress, in uint voxelColorU, in uint voxelNormalU) {
	// get brick address
       uint nodeColorU = imageLoad(nodePool_color, nodeAddress).x;
       memoryBarrier();
       
       ivec3 brickCoords = ivec3(uintXYZ10ToVec3(nodeColorU));
	   // posTex is the position in a node tile, posTex = (0,0,0) ~ (1,1,1) 
       uvec3 offVec = uvec3(2.0 * posTex);
	   uint off = offVec.x + 2U * offVec.y + 4U * offVec.z;

       //store VoxelColors in brick corners
       imageStore(brickPool_color,
             brickCoords  + 2 * ivec3(childOffsets[off]),
             convRGBA8ToVec4(voxelColorU) / 255.0);

       imageStore(brickPool_normal,
             brickCoords  + 2 * ivec3(childOffsets[off]),
             convRGBA8ToVec4(voxelNormalU) / 255.0);

       imageStore(brickPool_irradiance,
                  brickCoords  + 2 * ivec3(childOffsets[off]),
                  vec4(0.0, 0.0, 0.0, 1.0));
}

void main() {
  // Get the voxel's position and color from the voxel frag list.
  uint voxelPosU = imageLoad(voxelFragList_position, gl_VertexID).x;
  uvec3 voxelPos = uintXYZ10ToVec3(voxelPosU);
    
  uint voxelColorU = imageLoad(voxelFragTex_color, ivec3(voxelPos)).x;
  uint voxelNormalU = imageLoad(voxelFragTex_normal, ivec3(voxelPos)).x;
  memoryBarrier();

  vec3 posTex = vec3(voxelPos) / vec3(voxelGridResolution);

  uint onLevel = 0;
  int nodeAddress = traverseOctree_posOut(posTex, onLevel);
  
  storeInLeaf(posTex, nodeAddress, voxelColorU, voxelNormalU);
}
