
#define WITH_COLOR 1
#define WITH_NORMAL 1
#include "VS_InOut.hlsli"

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = input.pos;
	output.color = input.color;
	output.normal = input.normal;

	return output;
}