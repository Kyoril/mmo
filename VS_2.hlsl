struct VertexIn
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float3 normal : NORMAL;
	float3 binormal : BINORMAL;
	float3 tangent : TANGENT;
	uint4 boneIndices : BLENDINDICES;
	float4 boneWeights : BLENDWEIGHT;
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

cbuffer Bones
{
	column_major matrix matBone[128];
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	matrix boneMatrix = matBone[input.boneIndices[0]-1] * input.boneWeights[0];
	if(input.boneIndices[1] != 0) { boneMatrix += matBone[input.boneIndices[1]-1] * input.boneWeights[1]; }
	if(input.boneIndices[2] != 0) { boneMatrix += matBone[input.boneIndices[2]-1] * input.boneWeights[2]; }
	if(input.boneIndices[3] != 0) { boneMatrix += matBone[input.boneIndices[3]-1] * input.boneWeights[3]; }

	float4 transformedPos = mul(float4(input.pos.xyz, 1.0), boneMatrix);
	output.pos = mul(transformedPos, matWorld);
	output.worldPos = output.pos.xyz;
	output.viewDir = normalize(matInvView[3].xyz - output.worldPos);
	output.pos = mul(output.pos, matView);
	output.pos = mul(output.pos, matProj);
	output.color = input.color;

	return output;
}

