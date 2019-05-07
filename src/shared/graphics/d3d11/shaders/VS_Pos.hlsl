
#include "VS_InOut.hlsli"

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = input.pos;

	return output;
}