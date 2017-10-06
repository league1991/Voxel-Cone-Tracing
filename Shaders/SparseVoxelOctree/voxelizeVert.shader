#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;

//out vec3 worldPositionGeom;
//out vec3 normalGeom;

out VertexData{
	vec3 pos;
	vec3 normal;
	vec2 uv;
} Out;

void main() {
	Out.pos = vec3(M * vec4(position, 1));
	Out.normal = normalize(mat3(transpose(inverse(M))) * normal);
	Out.uv = vec2(0.0);// Unused temporarily.
	//gl_Position = P * V * vec4(worldPositionGeom, 1);
}
