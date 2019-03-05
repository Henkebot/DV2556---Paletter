RWTexture2D<float4> color : register(u0);
Texture2D<float4> color2 : register(t0);

// DCT buffers
StructuredBuffer<float> DCT_Matrix : register(t1);
StructuredBuffer<float> DCT_Matrix_Transpose : register(t2);
groupshared float4 Pixels[64];

groupshared float DCT_MatrixTempR[64];
groupshared float DCT_MatrixTempG[64];
groupshared float DCT_MatrixTempB[64];
groupshared float DCT_CoefficientsR[64];
groupshared float DCT_CoefficientsG[64];
groupshared float DCT_CoefficientsB[64];


groupshared int YQT[] =
{
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22,
							 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
};

cbuffer MOUSE : register(b0)
{
    int2 gazePos;
}

static float3x3 mYUV709n =
{ // Normalized
    0.2126, 0.7152, 0.0722,
    -0.1145721060573399, -0.3854278939426601, 0.5,
    0.5, -0.4541529083058166, -0.0458470916941834
};
float4 RGBtoYUV(float4 rgba)
{
    return float4(mul(mYUV709n, rgba.xyz), 1.0f) + float4(0, 0.5, 0.5, 0);
    //return float4(
    //    rgba.r * mYUV709n._m00 + rgba.g * mYUV709n._m01 + rgba.b * mYUV709n._m02,
    //    rgba.r * mYUV709n._m10 + rgba.g * mYUV709n._m11 + rgba.b * mYUV709n._m12,
    //    rgba.r * mYUV709n._m20 + rgba.g * mYUV709n._m21 + rgba.b * mYUV709n._m22,
    //    rgba.a
    //) + float4(0, 0.5, 0.5, 0);
}

static float3x3 mYUV709i =
{ // Inverse Normalized
    1, 0, 1.5748,
    1, -0.187324, -0.468124,
    1, 1.8556, 0
};
float4 YUVtoRGB(float4 yuva)
{
    yuva.gb -= 0.5;
    return float4(mul(mYUV709i, yuva.rgb), yuva.a);
    //return float4(
    //    yuva.r * mYUV709i._m00 + yuva.g * mYUV709i._m01 + yuva.b * mYUV709i._m02,
    //    yuva.r * mYUV709i._m10 + yuva.g * mYUV709i._m11 + yuva.b * mYUV709i._m12,
    //    yuva.r * mYUV709i._m20 + yuva.g * mYUV709i._m21 + yuva.b * mYUV709i._m22,
    //    yuva.a);
}

[numthreads(8, 8, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID,
	uint3 GroupThreadID : SV_GroupThreadID,
	uint3 GroupID : SV_GroupID,
	uint GroupIndex : SV_GroupIndex)
{
    int2 coord = GroupID.xy;

    coord <<= 3;
    float quality = (1.0f - ((float(length(coord - gazePos) / (200.0f)))));
    //GroupMemoryBarrierWithGroupSync();
   /* color[DispatchThreadID.xy] = float4(quality, 0, 0, 1.0f);
   return;*/
    quality *= 70.0f;


    quality = quality ? quality : 90;
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    int k;
    
    int yti = (YQT[GroupIndex] * quality + 50) / 100;
    yti = (yti < 1 ? 1 : yti > 255 ? 255 : yti);
    YQT[GroupIndex] = yti;
    
    
    coord += GroupThreadID.xy;
    Pixels[GroupIndex] = (RGBtoYUV(color2[coord]) * 255.0f) - float4(128.0f, 128.0f, 128.0f, 0.0f);
    

    GroupMemoryBarrierWithGroupSync();

    DCT_MatrixTempR[GroupIndex] = 0;
    DCT_MatrixTempG[GroupIndex] = 0;
    DCT_MatrixTempB[GroupIndex] = 0;
	[unroll]
    for (k = 0; k < 8; k++)
    {
        DCT_MatrixTempR[GroupIndex] += DCT_Matrix[GroupThreadID.y * 8 + k] * Pixels[k * 8 + GroupThreadID.x].r;
        DCT_MatrixTempG[GroupIndex] += DCT_Matrix[GroupThreadID.y * 8 + k] * Pixels[k * 8 + GroupThreadID.x].g;
        DCT_MatrixTempB[GroupIndex] += DCT_Matrix[GroupThreadID.y * 8 + k] * Pixels[k * 8 + GroupThreadID.x].b;
    }

    GroupMemoryBarrierWithGroupSync();

    DCT_CoefficientsR[GroupIndex] = 0;
    DCT_CoefficientsG[GroupIndex] = 0;
    DCT_CoefficientsB[GroupIndex] = 0;
	[unroll]
    for (k = 0; k < 8; k++)
    {
        DCT_CoefficientsR[GroupIndex] += DCT_MatrixTempR[GroupThreadID.y * 8 + k] * DCT_Matrix_Transpose[k * 8 + GroupThreadID.x];
        DCT_CoefficientsG[GroupIndex] += DCT_MatrixTempG[GroupThreadID.y * 8 + k] * DCT_Matrix_Transpose[k * 8 + GroupThreadID.x];
        DCT_CoefficientsB[GroupIndex] += DCT_MatrixTempB[GroupThreadID.y * 8 + k] * DCT_Matrix_Transpose[k * 8 + GroupThreadID.x];

    }
    // Now do we have the coefficients

    GroupMemoryBarrierWithGroupSync();

    float div = 1.0f / YQT[GroupIndex];
    DCT_CoefficientsR[GroupIndex] = round(DCT_CoefficientsR[GroupIndex] * div) * YQT[GroupIndex];
    DCT_CoefficientsG[GroupIndex] = round(DCT_CoefficientsG[GroupIndex] * div) * YQT[GroupIndex];
    DCT_CoefficientsB[GroupIndex] = round(DCT_CoefficientsB[GroupIndex] * div) * YQT[GroupIndex];

    GroupMemoryBarrierWithGroupSync();

    DCT_MatrixTempR[GroupIndex] = 0;
    DCT_MatrixTempG[GroupIndex] = 0;
    DCT_MatrixTempB[GroupIndex] = 0;
    [unroll]
    for (k = 0; k < 8; k++)
    {
        DCT_MatrixTempR[GroupIndex] += DCT_Matrix_Transpose[GroupThreadID.y * 8 + k] * DCT_CoefficientsR[k * 8 + GroupThreadID.x];
        DCT_MatrixTempG[GroupIndex] += DCT_Matrix_Transpose[GroupThreadID.y * 8 + k] * DCT_CoefficientsG[k * 8 + GroupThreadID.x];
        DCT_MatrixTempB[GroupIndex] += DCT_Matrix_Transpose[GroupThreadID.y * 8 + k] * DCT_CoefficientsB[k * 8 + GroupThreadID.x];

    }
    GroupMemoryBarrierWithGroupSync();

    Pixels[GroupIndex] = float4(0, 0, 0, 1.0f);
    [unroll]
    for (k = 0; k < 8; k++)
    {
        Pixels[GroupIndex].r += DCT_MatrixTempR[GroupThreadID.y * 8 + k] * DCT_Matrix[k * 8 + GroupThreadID.x];
        Pixels[GroupIndex].g += DCT_MatrixTempG[GroupThreadID.y * 8 + k] * DCT_Matrix[k * 8 + GroupThreadID.x];
        Pixels[GroupIndex].b += DCT_MatrixTempB[GroupThreadID.y * 8 + k] * DCT_Matrix[k * 8 + GroupThreadID.x];

    }
    Pixels[GroupIndex].r += 128.0f;
    Pixels[GroupIndex].g += 128.0f;
    Pixels[GroupIndex].b += 128.0f;
     
    Pixels[GroupIndex].r /= 255.0f;
    Pixels[GroupIndex].g /= 255.0f;
    Pixels[GroupIndex].b /= 255.0f;
    Pixels[GroupIndex] = YUVtoRGB(Pixels[GroupIndex]);
  
    color[coord] = Pixels[GroupIndex];
    

            
 
    

}