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


[numthreads(8, 8, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID,
	uint3 GroupThreadID : SV_GroupThreadID,
	uint3 GroupID : SV_GroupID,
	uint GroupIndex : SV_GroupIndex)
{
    int2 coord = GroupID.xy;

    coord *= 8;
    coord += GroupThreadID.xy;
   
    uint quality = ((float(length(coord - gazePos) / 1800.0))) * 100.0f;

    quality = quality ? quality : 90;
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    int k;
    
    int yti = (YQT[GroupIndex] * quality + 50) / 100;
    yti = (yti < 1 ? 1 : yti > 255 ? 255 : yti);
    YQT[GroupIndex] = yti;
    
    Pixels[GroupIndex] = (color2[coord] * 255.0f) - 128.0f;

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

  
    color[coord] = Pixels[GroupIndex];
    
   
    //Pixels[ GroupIndex];
            
 
    

}