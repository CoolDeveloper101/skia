#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct Uniforms {
    float testInput;
    half4 colorGreen;
    half4 colorRed;
};
struct Inputs {
};
struct Outputs {
    half4 sk_FragColor [[color(0)]];
};
fragment Outputs fragmentMain(Inputs _in [[stage_in]], constant Uniforms& _uniforms [[buffer(0)]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _out;
    (void)_out;
    float4 color;
    color = float4(_uniforms.colorGreen) * 0.5;
    color.w = 2.0;
    color.y = color.y / 0.25;
    color.yzw = color.yzw * float3(0.5);
    color.zywx = color.zywx + float4(0.25, 0.0, 0.0, 0.75);
    _out.sk_FragColor = all(color == float4(0.75, 1.0, 0.25, 1.0)) ? _uniforms.colorGreen : _uniforms.colorRed;
    return _out;
}
