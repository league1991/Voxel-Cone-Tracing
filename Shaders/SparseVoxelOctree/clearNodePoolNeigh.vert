#version 420 core

//layout(r32ui) uniform volatile uimageBuffer nodePool_color;
//layout(r32ui) uniform volatile uimageBuffer nodePool_next;
//layout(r32ui) uniform volatile uimageBuffer nodePool_normal;
layout(r32ui) uniform volatile uimageBuffer nodePool_X;
layout(r32ui) uniform volatile uimageBuffer nodePool_Y;
layout(r32ui) uniform volatile uimageBuffer nodePool_Z;
layout(r32ui) uniform volatile uimageBuffer nodePool_X_neg;
layout(r32ui) uniform volatile uimageBuffer nodePool_Y_neg;
layout(r32ui) uniform volatile uimageBuffer nodePool_Z_neg;


void main() {
  //imageStore(nodePool_color,gl_VertexID,uvec4(0));
  //imageStore(nodePool_next,gl_VertexID,uvec4(0));
  //imageStore(nodePool_normal,gl_VertexID,uvec4(0));
  imageStore(nodePool_X, gl_VertexID, uvec4(1));
  imageStore(nodePool_Y, gl_VertexID, uvec4(0));
  imageStore(nodePool_Z, gl_VertexID, uvec4(0));
  imageStore(nodePool_X_neg, gl_VertexID, uvec4(0));
  imageStore(nodePool_Y_neg, gl_VertexID, uvec4(0));
  imageStore(nodePool_Z_neg, gl_VertexID, uvec4(0));
}