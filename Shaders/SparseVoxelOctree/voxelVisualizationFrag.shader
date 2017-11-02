#version 430 core
in VoxelData{
	vec4 pos;
	vec4 color;
} In;

out vec4 pixelColor;

void main() {
	pixelColor = vec4(In.color.rgb, In.color.a);
}
