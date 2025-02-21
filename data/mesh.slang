struct GpuVertex {
	float3 position;
	float2 normalOct;
	float tangentDia;
}

struct VSOut {
	float3 normal;
}

struct GlobalUniforms {
	float4 sun;
	float3 camera;
	float4 cameraRight;
	float4 cameraUp;
}

struct AtmosphereUniforms {
	float4 planet;
	float4 coefsRay;
	float4 coefsMie;
	float4 camera;
	float radius;
	uint opticalDepthSamples;
	uint scatterPointSamples;
	float intensity;
}

ParameterBlock<GlobalUniforms> global;

[[shader("fragment")]]
void fragment() {

}
