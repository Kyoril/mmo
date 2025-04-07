static const float PI = 3.14159265359;

float select(bool expression, float whenTrue, float whenFalse) {
	return expression ? whenTrue : whenFalse;
}

cbuffer CameraParameters : register(b0)
{
	float3 cameraPos;	// Camera position in world space
	float fogStart;	// Distance of fog start
	float fogEnd;		// Distance of fog end
	float3 fogColor;	// Fog color
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float3 worldPos : TEXCOORD0;
	float3 viewDir : TEXCOORD1;
};

float4 main(VertexOut input) : SV_Target
{
	float4 outputColor = float4(1, 1, 1, 1);

	float3 V = normalize(input.viewDir);

	float opacity = 1.0;

	float3 baseColor = float3(1.0, 1.0, 1.0);

	if (opacity <= 0.333) { clip(-1); }
	baseColor = pow(baseColor, 2.2);
	float3 ao = float3(1.0, 1.0, 1.0);

	outputColor = float4(baseColor, opacity);
	return outputColor;
}
static const float PI = 3.14159265359;

float select(bool expression, float whenTrue, float whenFalse) {
	return expression ? whenTrue : whenFalse;
}

struct GBufferOutput
{
	float4 albedo : SV_Target0;    // RGB: Albedo, A: Opacity
	float4 normal : SV_Target1;    // RGB: Normal, A: Unused
	float4 material : SV_Target2;  // R: Metallic, G: Roughness, B: Specular, A: Ambient Occlusion
	float4 emissive : SV_Target3;  // RGB: Emissive, A: Unused
};

cbuffer CameraParameters : register(b0)
{
	float3 cameraPos;	// Camera position in world space
	float fogStart;	// Distance of fog start
	float fogEnd;		// Distance of fog end
	float3 fogColor;	// Fog color
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float3 worldPos : TEXCOORD0;
	float3 viewDir : TEXCOORD1;
};

GBufferOutput main(VertexOut input)
{
	float4 outputColor = float4(1, 1, 1, 1);

	float3 V = normalize(input.viewDir);

	float opacity = 1.0;

	float3 baseColor = float3(1.0, 1.0, 1.0);

	if (opacity <= 0.333) { clip(-1); }
	baseColor = pow(baseColor, 2.2);
	float3 ao = float3(1.0, 1.0, 1.0);

	GBufferOutput output;
	output.albedo = float4(baseColor, opacity);
	output.normal = float4(N * 0.5 + 0.5, 0.0);
	output.material = float4(metallic, roughness, specular, 1.0);
	output.emissive = float4(0.0, 0.0, 0.0, 0.0);
	return output;
}
