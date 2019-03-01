RWTexture2D<float4> color : register(u0);
Texture2D<float4> color2 : register(t0);

static const float PI = 3.14159265f;

static int YQT[] =
{
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22,
							 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
};

static const float S[] =
{
    0.353553390593273762200422,
	0.254897789552079584470970,
	0.270598050073098492199862,
	0.300672443467522640271861,
	0.353553390593273762200422,
	0.449988111568207852319255,
	0.653281482438188263928322,
	1.281457723870753089398043,
};

static const float A[] =
{
    -1,
	0.707106781186547524400844,
	0.541196100146196984399723,
	0.707106781186547524400844,
	1.306562964876376527856643,
	0.382683432365089771728460,
};
void DCTFast(inout float Input[8])
{
    const float v0 =
    Input[0]
    +

    Input[7];
    const float v1 =
    Input[1]
    +

    Input[6];
    const float v2 =
    Input[2]
    +

    Input[5];
    const float v3 =
    Input[3]
    +

    Input[4];
    const float v4 =
    Input[3]
    -

    Input[4];
    const float v5 =
    Input[2]
    -

    Input[5];
    const float v6 =
    Input[1]
    -

    Input[6];
    const float v7 =
    Input[0]
    -

    Input[7];
	
    const float v8 = v0 + v3;
    const float v9 = v1 + v2;
    const float v10 = v1 - v2;
    const float v11 = v0 - v3;
    const float v12 = -v4 - v5;
    const float v13 = (v5 + v6) * A[3];
    const float v14 = v6 + v7;
	
    const float v15 = v8 + v9;
    const float v16 = v8 - v9;
    const float v17 = (v10 + v11) * A[1];
    const float v18 = (v12 + v14) * A[5];
	
    const float v19 = -v12 * A[2] - v18;
    const float v20 = v14 * A[4] - v18;
	
    const float v21 = v17 + v11;
    const float v22 = v11 - v17;
    const float v23 = v13 + v7;
    const float v24 = v7 - v13;
	
    const float v25 = v19 + v24;
    const float v26 = v23 + v20;
    const float v27 = v23 - v20;
    const float v28 = v24 - v19;
	
    Input[0] = S[0] *
v15;
    Input[1] = S[1] *
v26;
    Input[2] = S[2] *
v21;
    Input[3] = S[3] *
v28;
    Input[4] = S[4] *
v16;
    Input[5] = S[5] *
v25;
    Input[6] = S[6] *
v22;
    Input[7] = S[7] *
v27;
}

#define SQRT2 1.4142135623730950488016887242097
#define SQRT8 2.8284271247461900976033774484194

void DCT(in float Input[8][8], out float Output[8][8])
{
    int i, j;
    float dct[8][8];

    float ci, cj, dct1, sum;

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 8; j++)
        {
            if (i == 0)
            {
                ci = 1.0 / SQRT8;
            }
            else
            {
                ci = SQRT2 / SQRT8;
            }

            if (j == 0)
            {
                cj = 1.0 / SQRT8;
            }
            else
            {
                cj = SQRT2 / SQRT8;
            }

            sum = 0.0f;
            for (int k = 0; k < 8; k++)
            {
                for (int l = 0; l < 8; l++)
                {
                    dct1 = (Input[k][l]
                    - 127) * cos((2.0 * k + 1) * i * PI / (2 * 8)) *
						cos((2 * l + 1) * j * PI / (2 * 8));
                    sum += dct1;
                }
            }
            dct[i][j] = ci * cj * sum;
        }
    }
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 8; j++)
        {
            Input[i][j] = round(dct[i][j] / YQT[i + j * 8]) * YQT[i + j * 8];
        }
    }

    int u, v;
    float s;

    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++)
        {
            s = 0;

            for (u = 0; u < 8; u++)
                for (v = 0; v < 8; v++)
                    s += Input[u][v] * cos((2 * i + 1) * u * PI / 16) *
					cos((2 * j + 1) * v * PI / 16) *
					((u == 0) ? 1 / SQRT2 : 1.) *
					((v == 0) ? 1 / SQRT2 : 1.);

            Output[i][j] = s / 4;
            Output[i][j] += 127.0f;

        }
}
cbuffer MOUSE : register(b0)
{
    int2 gazePos;
}


[numthreads(1, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID,
	uint3 GroupThreadID : SV_GroupThreadID,
	uint3 GroupID : SV_GroupID,
	uint GroupIndex : SV_GroupIndex)
{
     int2 coord = DispatchThreadID.xy;

    coord.x *= 8;
    coord.y *= 8;
    
    uint quality = (1.0f - (float(length(coord - gazePos) / 1800.0))) * 100.0f;

    quality = quality ? quality : 90;
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    int i, j;

    for (i = 0; i < 64; i++)
    {
        int yti = (YQT[i] * quality + 50) / 100;
        yti = (yti < 1 ? 1 : yti > 255 ? 255 : yti);
        YQT[i] = yti;
    }

  

    float InputR[8][8];

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 8; j++)
        {
            float2 finalCoord;
            finalCoord.x = coord.x + i;
            finalCoord.y = coord.y + j;
            InputR[i][j] = (color2[finalCoord][GroupThreadID.x] * 255.0f);

            
        }

    }
    float OutputR[8][8];
   
    DCT(InputR, OutputR);

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 8; j++)
        {
            float2 finalCoord;
            finalCoord.x = coord.x + i;
            finalCoord.y = coord.y + j;
          
            float fC = OutputR[i][j] / 255.0f;
            
      
            color[finalCoord] = float4(fC, fC, fC, 1.0f);
            
        }

    }

    

}