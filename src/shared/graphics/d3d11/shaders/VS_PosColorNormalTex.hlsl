
#define WITH_COLOR 1
#define WITH_NORMAL 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = input.pos;
	output.color = input.color;
	output.normal = input.normal;
	output.uv1 = input.uv1;

	return output;
}