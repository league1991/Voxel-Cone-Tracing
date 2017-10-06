#version 430 core

layout(r32ui) uniform uimage3D voxelFragTex_color;
layout(r32ui) uniform uimage3D voxelFragTex_normal;

void main() {
  int size = imageSize(voxelFragTex_color).x;
  ivec3 texCoord = ivec3(0);
  texCoord.x = gl_VertexID % size;
  texCoord.y = (gl_VertexID / size) % size;
  texCoord.z = gl_VertexID / (size * size);

  imageStore(voxelFragTex_color, texCoord, uvec4(0));
  imageStore(voxelFragTex_normal, texCoord, uvec4(0));
}
