
#define WITH_COLOR 1
#include "VS_InOut.hlsli"

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = input.pos;
	output.color= input.color;

	return output;
}