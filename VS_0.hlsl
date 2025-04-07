struct VertexIn
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float3 normal : NORMAL;
	float3 binormal : BINORMAL;
	float3 tangent : TANGENT;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float3 worldPos : TEXCOORD0;
	float3 viewDir : TEXCOORD1;
};

cbuffer Matrices
{
	column_major matrix matWorld;
	column_major matrix matView;
	column_major matrix matProj;
	column_major matrix matInvView;
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	float4 transformedPos = float4(input.pos.xyz, 1.0);
	output.pos = mul(transformedPos, matWorld);
	output.worldPos = output.pos.xyz;
	output.viewDir = normalize(matInvView[3].xyz - output.worldPos);
	output.pos = mul(output.pos, matView);
	output.pos = mul(output.pos, matProj);
	output.color = input.color;

	return output;
}

