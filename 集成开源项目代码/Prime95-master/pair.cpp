/**************************************************************
 *
 *	pair.cpp
 *
 *	ECM, P-1, and P+1 prime pairing routines
 *
 *	Important ideas courtesy of Pavel Atnashev, Mihai Preda, and myself.
 *
 *	c. 2021-2023 Mersenne Research, Inc.
 *	All Rights Reserved.
 *
 *************************************************************/

#include "common.h"
#include "gwnum.h"
#include "hwloc.h"
#include "commonc.h"
#include "pair.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/circular_buffer.hpp>

/* Handy macros/routines to improve readability */

// Courtesy of Stanford bit hacks, fails if input is zero
uint32_t next_pow2 (uint32_t v) { v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++; return (v); }

#define ONE_TIMES_BASE	1
#define MAX_PASSES	1
#define ON_THE_FLY_CAN_PAIR_TO			// Turn off if doing a non-windowed search (off is not fully implemented for windowed)
#define FIRST_EXIT	0
#define MAX_LINKS	500			// Controls how hard we search for another pairing (especially useful when there are lots of relocs)

/**********************************************************************************************************************/
/*                                      Data and routines to estimate pairing percentage                              */
/**********************************************************************************************************************/

// Routines to estimate pairing percentage and pairing runtime.  First we benchmarked several combinations on a circa 2015 Intel I5 CPU.
// We benched 3 second missing prime values using D=30,D=210,D=2310. Five B1 values: B1=10K,100K,1M,10M,100M, and 4 B2 values: B2=20x,50x,100x,200x.
// For each combination above we benched full usage of the 20 different relp_set_easyXX.

struct pair_data {
	float	pair_pct;	// Pairing percentage (from 0.0 to 1.0)
	float	pair_runtime;	// Pairing runtime.  Later we'll convert this to equivalent estimated #FFTs/#squarings.
};
struct pair_data pair_data[3][5][4][20] =	// Actual pairing data from an Intel I5 CPU.
{
 {	// D=30 (second missing prime = 11)
  {	// B1=10K
   {{.4020f,0.009f},{.5823f,0.006f},{.6987f,0.006f},{.7848f,0.006f},{.8506f,0.006f},{.9027f,0.007f},{.9447f,0.009f},{.9664f,0.011f},{.9782f,0.012f},{.9795f,0.013f},{.9851f,0.013f},{.9884f,0.015f},{.9910f,0.016f},{.9912f,0.016f},{.9918f,0.016f},{.9919f,0.017f},{.9922f,0.017f},{.9927f,0.018f},{.9928f,0.019f},{.9930f,0.019f}},
   {{.4317f,0.017f},{.6034f,0.015f},{.7152f,0.016f},{.7993f,0.015f},{.8638f,0.017f},{.9186f,0.018f},{.9576f,0.021f},{.9814f,0.024f},{.9903f,0.030f},{.9928f,0.029f},{.9946f,0.033f},{.9960f,0.035f},{.9960f,0.036f},{.9964f,0.036f},{.9966f,0.037f},{.9970f,0.037f},{.9974f,0.037f},{.9975f,0.039f},{.9975f,0.039f},{.9974f,0.041f}},
   {{.4321f,0.034f},{.6010f,0.031f},{.7124f,0.031f},{.7958f,0.031f},{.8594f,0.033f},{.9148f,0.036f},{.9565f,0.043f},{.9842f,0.052f},{.9932f,0.068f},{.9970f,0.062f},{.9981f,0.069f},{.9987f,0.073f},{.9989f,0.072f},{.9986f,0.074f},{.9983f,0.076f},{.9984f,0.079f},{.9985f,0.078f},{.9985f,0.081f},{.9985f,0.082f},{.9983f,0.083f}},
   {{.4247f,0.064f},{.5909f,0.059f},{.7008f,0.060f},{.7838f,0.058f},{.8477f,0.064f},{.9035f,0.070f},{.9478f,0.085f},{.9797f,0.106f},{.9947f,0.141f},{.9977f,0.139f},{.9986f,0.157f},{.9987f,0.169f},{.9988f,0.173f},{.9990f,0.174f},{.9992f,0.176f},{.9991f,0.176f},{.9991f,0.179f},{.9988f,0.185f},{.9988f,0.191f},{.9989f,0.192f}}
  },
  {	// B1=100K
   {{.3385f,0.046f},{.5107f,0.043f},{.6250f,0.043f},{.7123f,0.046f},{.7801f,0.048f},{.8392f,0.049f},{.8867f,0.053f},{.9232f,0.057f},{.9474f,0.066f},{.9604f,0.060f},{.9715f,0.064f},{.9806f,0.067f},{.9866f,0.069f},{.9886f,0.069f},{.9903f,0.072f},{.9924f,0.073f},{.9929f,0.073f},{.9944f,0.072f},{.9946f,0.074f},{.9953f,0.076f}},
   {{.3702f,0.135f},{.5370f,0.126f},{.6488f,0.132f},{.7356f,0.126f},{.8006f,0.134f},{.8581f,0.142f},{.9044f,0.151f},{.9408f,0.166f},{.9652f,0.198f},{.9778f,0.188f},{.9869f,0.203f},{.9927f,0.218f},{.9958f,0.231f},{.9969f,0.234f},{.9978f,0.243f},{.9985f,0.248f},{.9987f,0.253f},{.9986f,0.248f},{.9989f,0.263f},{.9989f,0.266f}},
   {{.3776f,0.308f},{.5396f,0.271f},{.6495f,0.270f},{.7332f,0.268f},{.7987f,0.277f},{.8558f,0.291f},{.9027f,0.311f},{.9402f,0.355f},{.9664f,0.412f},{.9808f,0.427f},{.9908f,0.484f},{.9958f,0.543f},{.9977f,0.562f},{.9985f,0.570f},{.9989f,0.595f},{.9992f,0.621f},{.9992f,0.626f},{.9993f,0.634f},{.9994f,0.635f},{.9993f,0.670f}},
   {{.3744f,0.612f},{.5337f,0.573f},{.6420f,0.527f},{.7250f,0.527f},{.7903f,0.557f},{.8468f,0.575f},{.8935f,0.629f},{.9320f,0.737f},{.9606f,0.853f},{.9776f,0.921f},{.9909f,1.057f},{.9972f,1.240f},{.9988f,1.328f},{.9993f,1.411f},{.9995f,1.464f},{.9996f,1.512f},{.9997f,1.523f},{.9998f,1.520f},{.9998f,1.567f},{.9997f,1.606f}}
  },
  {	// B1=1M
   {{.2919f,0.407f},{.4545f,0.398f},{.5660f,0.408f},{.6532f,0.384f},{.7217f,0.403f},{.7813f,0.415f},{.8309f,0.423f},{.8721f,0.448f},{.9037f,0.503f},{.9233f,0.490f},{.9413f,0.505f},{.9581f,0.533f},{.9697f,0.549f},{.9755f,0.533f},{.9804f,0.570f},{.9850f,0.577f},{.9872f,0.585f},{.9900f,0.571f},{.9911f,0.589f},{.9927f,0.598f}},
   {{.3261f,1.266f},{.4857f,1.163f},{.5954f,1.132f},{.6796f,1.128f},{.7460f,1.125f},{.8035f,1.188f},{.8516f,1.240f},{.8917f,1.327f},{.9229f,1.453f},{.9428f,1.439f},{.9611f,1.581f},{.9766f,1.691f},{.9855f,1.829f},{.9899f,1.880f},{.9931f,1.989f},{.9953f,2.050f},{.9964f,2.083f},{.9974f,2.088f},{.9978f,2.153f},{.9982f,2.204f}},
   {{.3356f,3.045f},{.4912f,2.729f},{.5983f,2.591f},{.6812f,2.490f},{.7466f,2.508f},{.8035f,2.602f},{.8513f,2.698f},{.8912f,2.846f},{.9230f,3.195f},{.9440f,3.211f},{.9636f,3.607f},{.9806f,4.114f},{.9900f,4.526f},{.9940f,4.687f},{.9965f,5.193f},{.9978f,5.294f},{.9985f,5.459f},{.9989f,5.594f},{.9991f,5.857f},{.9993f,5.976f}},
   {{.3358f,6.742f},{.4884f,5.891f},{.5936f,5.285f},{.6753f,5.154f},{.7405f,5.291f},{.7969f,5.464f},{.8444f,5.738f},{.8843f,5.920f},{.9167f,6.731f},{.9386f,7.107f},{.9595f,7.889f},{.9789f,9.285f},{.9905f,10.566f},{.9953f,11.446f},{.9977f,12.607f},{.9988f,13.487f},{.9992f,14.121f},{.9994f,14.980f},{.9995f,15.382f},{.9996f,15.448f}}
  },
  {	// B1=10M
   {{.2564f,3.482f},{.4103f,3.409f},{.5176f,3.488f},{.6031f,3.445f},{.6713f,3.568f},{.7306f,3.743f},{.7806f,3.739f},{.8229f,3.956f},{.8575f,4.166f},{.8808f,4.179f},{.9038f,4.306f},{.9266f,4.649f},{.9437f,4.588f},{.9540f,4.769f},{.9628f,4.816f},{.9707f,5.034f},{.9751f,4.913f},{.9799f,5.111f},{.9825f,5.345f},{.9854f,5.391f}},
   {{.2916f,12.522f},{.4441f,11.257f},{.5499f,11.233f},{.6324f,10.916f},{.6985f,10.850f},{.7558f,11.011f},{.8043f,11.317f},{.8455f,11.763f},{.8793f,12.696f},{.9026f,12.462f},{.9255f,13.273f},{.9479f,14.330f},{.9639f,14.857f},{.9735f,15.556f},{.9814f,16.526f},{.9871f,17.410f},{.9904f,17.602f},{.9931f,18.329f},{.9947f,18.799f},{.9958f,19.756f}},
   {{.3028f,30.741f},{.4516f,26.311f},{.5553f,24.774f},{.6363f,24.176f},{.7014f,23.813f},{.7578f,24.065f},{.8058f,24.055f},{.8467f,24.658f},{.8805f,26.859f},{.9042f,26.720f},{.9276f,28.529f},{.9510f,31.939f},{.9679f,34.378f},{.9783f,36.656f},{.9865f,39.605f},{.9918f,44.059f},{.9946f,45.547f},{.9964f,48.442f},{.9974f,51.205f},{.9981f,52.272f}},
   {{.3047f,64.195f},{.4507f,54.781f},{.5526f,50.058f},{.6328f,48.305f},{.6972f,48.545f},{.7531f,48.440f},{.8008f,49.302f},{.8414f,51.001f},{.8755f,55.462f},{.8994f,56.468f},{.9234f,60.142f},{.9477f,67.347f},{.9659f,76.906f},{.9777f,85.195f},{.9874f,94.663f},{.9936f,106.131f},{.9964f,116.258f},{.9980f,124.237f},{.9987f,130.977f},{.9991f,136.343f}}
  },
  {	// B1=100M
   {{.2285f,34.985f},{.3737f,32.233f},{.4768f,33.024f},{.5602f,33.056f},{.6273f,33.827f},{.6860f,35.215f},{.7360f,35.213f},{.7788f,35.723f},{.8149f,37.489f},{.8401f,37.058f},{.8657f,38.676f},{.8920f,39.409f},{.9129f,41.025f},{.9269f,41.051f},{.9397f,42.416f},{.9510f,43.078f},{.9583f,44.073f},{.9658f,45.398f},{.9704f,46.703f},{.9751f,46.048f}},
   {{.2637f,119.773f},{.4090f,112.586f},{.5114f,105.518f},{.5924f,103.470f},{.6575f,104.168f},{.7143f,103.270f},{.7627f,104.206f},{.8041f,107.287f},{.8392f,112.085f},{.8641f,112.983f},{.8893f,116.290f},{.9151f,122.392f},{.9352f,127.213f},{.9490f,132.079f},{.9616f,135.026f},{.9717f,143.431f},{.9781f,145.500f},{.9839f,153.622f},{.9875f,161.267f},{.9905f,169.527f}},
   {{.2762f,289.504f},{.4184f,255.258f},{.5187f,233.603f},{.5982f,223.489f},{.6623f,223.295f},{.7182f,224.433f},{.7659f,221.896f},{.8069f,227.028f},{.8418f,242.037f},{.8667f,239.533f},{.8920f,247.107f},{.9181f,267.171f},{.9388f,284.478f},{.9533f,302.676f},{.9665f,320.299f},{.9771f,347.221f},{.9838f,361.962f},{.9893f,400.171f},{.9925f,409.910f},{.9948f,440.442f}},
   {{.2796f,647.529f},{.4191f,539.859f},{.5178f,498.221f},{.5963f,469.495f},{.6596f,467.042f},{.7150f,460.950f},{.7624f,473.609f},{.8031f,476.688f},{.8381f,513.054f},{.8630f,512.144f},{.8885f,540.267f},{.9150f,566.853f},{.9362f,627.423f},{.9514f,652.421f},{.9656f,741.484f},{.9773f,803.843f},{.9849f,878.025f},{.9912f,977.933f},{.9947f,1075.790f},{.9968f,1146.934f}}
  }
 },

 {	// D=210 (second missing prime = 13)
  {	// B1=10K
   {{.4236f,0.007f},{.5999f,0.005f},{.7201f,0.005f},{.8020f,0.006f},{.8692f,0.006f},{.9158f,0.009f},{.9426f,0.019f},{.9518f,0.023f},{.9601f,0.030f},{.9603f,0.036f},{.9708f,0.034f},{.9724f,0.037f},{.9740f,0.038f},{.9749f,0.038f},{.9748f,0.043f},{.9751f,0.047f},{.9750f,0.047f},{.9749f,0.048f},{.9756f,0.051f},{.9758f,0.051f}},
   {{.4471f,0.017f},{.6176f,0.017f},{.7377f,0.017f},{.8203f,0.019f},{.8867f,0.021f},{.9386f,0.029f},{.9735f,0.040f},{.9861f,0.045f},{.9904f,0.054f},{.9906f,0.060f},{.9938f,0.057f},{.9943f,0.063f},{.9952f,0.061f},{.9953f,0.064f},{.9949f,0.073f},{.9949f,0.074f},{.9954f,0.077f},{.9952f,0.078f},{.9952f,0.082f},{.9953f,0.081f}},
   {{.4447f,0.036f},{.6152f,0.032f},{.7317f,0.035f},{.8157f,0.035f},{.8804f,0.039f},{.9322f,0.049f},{.9712f,0.071f},{.9890f,0.090f},{.9937f,0.104f},{.9949f,0.102f},{.9968f,0.103f},{.9970f,0.113f},{.9977f,0.114f},{.9976f,0.118f},{.9978f,0.123f},{.9975f,0.130f},{.9975f,0.134f},{.9977f,0.133f},{.9979f,0.137f},{.9978f,0.140f}},
   {{.4365f,0.065f},{.6049f,0.059f},{.7183f,0.059f},{.8019f,0.061f},{.8693f,0.067f},{.9215f,0.076f},{.9641f,0.100f},{.9886f,0.146f},{.9962f,0.184f},{.9979f,0.178f},{.9987f,0.198f},{.9990f,0.207f},{.9990f,0.211f},{.9990f,0.216f},{.9990f,0.225f},{.9990f,0.233f},{.9991f,0.237f},{.9991f,0.238f},{.9992f,0.247f},{.9991f,0.253f}}
  },
  {	// B1=100K
   {{.3530f,0.041f},{.5252f,0.044f},{.6439f,0.044f},{.7326f,0.042f},{.8038f,0.043f},{.8595f,0.046f},{.9069f,0.054f},{.9410f,0.062f},{.9578f,0.075f},{.9698f,0.072f},{.9778f,0.079f},{.9819f,0.084f},{.9847f,0.088f},{.9860f,0.092f},{.9869f,0.096f},{.9873f,0.101f},{.9876f,0.104f},{.9877f,0.106f},{.9883f,0.109f},{.9883f,0.111f}},
   {{.3836f,0.123f},{.5512f,0.123f},{.6670f,0.124f},{.7520f,0.123f},{.8209f,0.123f},{.8758f,0.131f},{.9222f,0.154f},{.9568f,0.174f},{.9733f,0.213f},{.9836f,0.204f},{.9892f,0.229f},{.9919f,0.243f},{.9936f,0.242f},{.9942f,0.248f},{.9945f,0.260f},{.9945f,0.269f},{.9945f,0.274f},{.9943f,0.282f},{.9945f,0.287f},{.9945f,0.297f}},
   {{.3875f,0.270f},{.5530f,0.258f},{.6660f,0.255f},{.7492f,0.257f},{.8181f,0.263f},{.8735f,0.281f},{.9204f,0.317f},{.9572f,0.352f},{.9764f,0.427f},{.9892f,0.443f},{.9942f,0.496f},{.9961f,0.544f},{.9971f,0.557f},{.9974f,0.571f},{.9977f,0.593f},{.9977f,0.598f},{.9978f,0.607f},{.9979f,0.611f},{.9980f,0.633f},{.9980f,0.641f}},
   {{.3836f,0.522f},{.5458f,0.464f},{.6582f,0.474f},{.7413f,0.480f},{.8097f,0.495f},{.8651f,0.528f},{.9124f,0.592f},{.9507f,0.673f},{.9733f,0.846f},{.9896f,0.934f},{.9959f,1.131f},{.9979f,1.264f},{.9985f,1.337f},{.9987f,1.370f},{.9988f,1.439f},{.9989f,1.475f},{.9989f,1.503f},{.9989f,1.517f},{.9989f,1.566f},{.9989f,1.572f}}
  },
  {	// B1=1M
   {{.3033f,0.316f},{.4674f,0.325f},{.5843f,0.337f},{.6712f,0.386f},{.7432f,0.359f},{.8018f,0.372f},{.8522f,0.405f},{.8944f,0.419f},{.9207f,0.513f},{.9452f,0.445f},{.9614f,0.493f},{.9721f,0.521f},{.9796f,0.517f},{.9843f,0.511f},{.9876f,0.533f},{.9900f,0.535f},{.9916f,0.545f},{.9928f,0.554f},{.9936f,0.567f},{.9941f,0.576f}},
   {{.3359f,1.043f},{.4972f,0.989f},{.6106f,0.985f},{.6954f,1.009f},{.7651f,1.039f},{.8222f,1.065f},{.8712f,1.163f},{.9121f,1.261f},{.9384f,1.471f},{.9613f,1.433f},{.9756f,1.594f},{.9843f,1.731f},{.9896f,1.771f},{.9926f,1.770f},{.9944f,1.881f},{.9957f,1.905f},{.9964f,1.925f},{.9970f,1.964f},{.9974f,2.023f},{.9976f,2.048f}},
   {{.3439f,2.431f},{.5019f,2.183f},{.6135f,2.156f},{.6969f,2.105f},{.7654f,2.129f},{.8221f,2.229f},{.8707f,2.392f},{.9117f,2.634f},{.9392f,2.997f},{.9642f,3.126f},{.9798f,3.671f},{.9888f,4.235f},{.9937f,4.493f},{.9959f,4.667f},{.9972f,5.030f},{.9979f,5.189f},{.9983f,5.286f},{.9986f,5.413f},{.9987f,5.564f},{.9988f,5.626f}},
   {{.3434f,5.287f},{.4982f,4.527f},{.6082f,4.313f},{.6908f,4.199f},{.7589f,4.320f},{.8153f,4.466f},{.8638f,4.828f},{.9052f,5.290f},{.9338f,5.945f},{.9605f,6.448f},{.9787f,7.847f},{.9900f,9.579f},{.9955f,10.795f},{.9977f,11.915f},{.9987f,13.093f},{.9991f,13.671f},{.9993f,14.050f},{.9994f,14.379f},{.9995f,14.850f},{.9995f,15.095f}}
  },
  {	// B1=10M
   {{.2661f,2.778f},{.4207f,2.750f},{.5342f,2.859f},{.6202f,2.930f},{.6916f,3.157f},{.7509f,3.146f},{.8021f,3.298f},{.8462f,3.479f},{.8771f,3.881f},{.9074f,3.698f},{.9300f,3.957f},{.9474f,4.206f},{.9602f,4.174f},{.9689f,4.145f},{.9757f,4.328f},{.9807f,4.385f},{.9843f,4.391f},{.9870f,4.407f},{.9889f,4.568f},{.9905f,4.616f}},
   {{.2998f,10.048f},{.4536f,9.523f},{.5639f,9.263f},{.6477f,9.267f},{.7169f,9.506f},{.7744f,9.617f},{.8240f,10.095f},{.8667f,10.722f},{.8972f,11.815f},{.9266f,11.912f},{.9482f,13.061f},{.9644f,14.142f},{.9754f,14.650f},{.9822f,15.039f},{.9872f,15.957f},{.9904f,16.405f},{.9925f,16.797f},{.9940f,16.990f},{.9951f,17.723f},{.9958f,17.969f}},
   {{.3097f,23.948f},{.4607f,20.962f},{.5687f,20.142f},{.6511f,19.663f},{.7193f,19.828f},{.7760f,20.330f},{.8251f,21.130f},{.8676f,22.656f},{.8986f,24.461f},{.9285f,25.575f},{.9512f,27.969f},{.9687f,31.764f},{.9804f,34.983f},{.9874f,37.240f},{.9920f,40.946f},{.9946f,43.193f},{.9961f,45.023f},{.9971f,45.208f},{.9977f,48.427f},{.9981f,49.176f}},
   {{.3112f,54.702f},{.4594f,45.654f},{.5657f,41.764f},{.6472f,39.435f},{.7146f,39.813f},{.7710f,40.770f},{.8198f,42.311f},{.8622f,43.985f},{.8936f,47.899f},{.9242f,50.704f},{.9480f,56.708f},{.9672f,66.023f},{.9807f,74.822f},{.9890f,85.198f},{.9943f,99.991f},{.9968f,108.995f},{.9981f,119.064f},{.9987f,126.055f},{.9991f,126.558f},{.9993f,135.073f}}
  },
  {	// B1=100M
   {{.2369f,25.605f},{.3830f,25.429f},{.4919f,26.102f},{.5763f,26.437f},{.6467f,28.778f},{.7056f,28.573f},{.7570f,29.180f},{.8018f,30.627f},{.8351f,32.949f},{.8682f,32.601f},{.8945f,34.090f},{.9168f,36.011f},{.9343f,36.367f},{.9471f,36.146f},{.9580f,37.880f},{.9659f,38.111f},{.9719f,38.258f},{.9766f,38.881f},{.9802f,40.307f},{.9831f,40.680f}},
   {{.2707f,97.362f},{.4175f,89.292f},{.5243f,87.622f},{.6065f,85.029f},{.6749f,88.909f},{.7320f,87.452f},{.7818f,89.056f},{.8251f,94.539f},{.8578f,101.617f},{.8898f,100.292f},{.9154f,111.052f},{.9370f,117.915f},{.9533f,123.326f},{.9649f,125.688f},{.9743f,134.927f},{.9805f,139.441f},{.9849f,142.268f},{.9881f,145.509f},{.9905f,153.981f},{.9921f,154.067f}},
   {{.2820f,241.746f},{.4264f,208.046f},{.5310f,187.964f},{.6118f,190.242f},{.6791f,181.797f},{.7354f,190.315f},{.7846f,189.399f},{.8275f,203.968f},{.8602f,205.381f},{.8922f,220.368f},{.9183f,228.129f},{.9406f,260.695f},{.9578f,278.374f},{.9701f,297.302f},{.9800f,328.770f},{.9862f,352.209f},{.9903f,373.967f},{.9930f,390.452f},{.9948f,412.183f},{.9960f,425.944f}},
   {{.2850f,542.623f},{.4267f,431.356f},{.5297f,402.189f},{.6095f,381.768f},{.6760f,378.594f},{.7319f,377.107f},{.7806f,389.844f},{.8233f,401.469f},{.8562f,425.821f},{.8884f,436.827f},{.9150f,472.993f},{.9381f,517.073f},{.9563f,566.171f},{.9698f,623.665f},{.9810f,712.853f},{.9882f,806.733f},{.9928f,899.993f},{.9955f,983.873f},{.9972f,1083.705f},{.9981f,1142.023f}}
  }
 },

 {	// D=2310 (second missing prime = 17)
  {	// B1=10K
   {{.4431f,0.012f},{.6128f,0.009f},{.7215f,0.009f},{.7838f,0.010f},{.8127f,0.010f},{.8159f,0.010f},{.8164f,0.011f},{.8174f,0.011f},{.8179f,0.011f},{.8202f,0.011f},{.8421f,0.012f},{.8421f,0.012f},{.8458f,0.013f},{.8458f,0.013f},{.8458f,0.013f},{.8458f,0.013f},{.8458f,0.013f},{.8458f,0.013f},{.8483f,0.014f},{.8483f,0.014f}},
   {{.4654f,0.024f},{.6346f,0.025f},{.7453f,0.026f},{.8213f,0.030f},{.8708f,0.037f},{.8946f,0.054f},{.8971f,0.055f},{.8901f,0.040f},{.8953f,0.102f},{.8947f,0.045f},{.9133f,0.059f},{.9135f,0.059f},{.9182f,0.071f},{.9182f,0.071f},{.9185f,0.075f},{.9185f,0.075f},{.9185f,0.076f},{.9185f,0.077f},{.9217f,0.083f},{.9217f,0.084f}},
   {{.4595f,0.060f},{.6296f,0.062f},{.7427f,0.067f},{.8218f,0.078f},{.8820f,0.118f},{.9210f,0.283f},{.9384f,0.361f},{.9385f,0.338f},{.9465f,0.459f},{.9404f,0.361f},{.9536f,0.376f},{.9545f,0.391f},{.9582f,0.406f},{.9584f,0.409f},{.9588f,0.435f},{.9589f,0.431f},{.9589f,0.435f},{.9586f,0.432f},{.9604f,0.456f},{.9604f,0.457f}},
   {{.4488f,0.159f},{.6181f,0.160f},{.7320f,0.174f},{.8137f,0.214f},{.8777f,0.429f},{.9259f,0.837f},{.9560f,0.900f},{.9647f,0.902f},{.9721f,0.881f},{.9711f,0.927f},{.9782f,0.865f},{.9777f,0.957f},{.9804f,0.920f},{.9800f,0.957f},{.9797f,1.037f},{.9800f,1.042f},{.9802f,1.072f},{.9800f,1.087f},{.9811f,1.111f},{.9810f,1.120f}}
  },
  {	// B1=100K
   {{.3668f,0.062f},{.5408f,0.063f},{.6595f,0.063f},{.7461f,0.068f},{.8130f,0.075f},{.8638f,0.086f},{.8976f,0.109f},{.9109f,0.141f},{.9249f,0.175f},{.9271f,0.208f},{.9412f,0.252f},{.9434f,0.281f},{.9480f,0.295f},{.9483f,0.308f},{.9498f,0.321f},{.9499f,0.330f},{.9500f,0.340f},{.9501f,0.348f},{.9522f,0.368f},{.9523f,0.374f}},
   {{.3940f,0.222f},{.5656f,0.236f},{.6812f,0.247f},{.7669f,0.255f},{.8345f,0.282f},{.8888f,0.339f},{.9307f,0.463f},{.9576f,0.624f},{.9726f,0.717f},{.9778f,0.811f},{.9852f,0.849f},{.9872f,0.921f},{.9892f,0.914f},{.9897f,0.960f},{.9903f,1.031f},{.9905f,1.064f},{.9907f,1.076f},{.9904f,1.108f},{.9912f,1.147f},{.9911f,1.187f}},
   {{.3984f,0.507f},{.5661f,0.513f},{.6798f,0.513f},{.7648f,0.531f},{.8323f,0.570f},{.8869f,0.648f},{.9316f,0.770f},{.9633f,0.976f},{.9796f,1.180f},{.9875f,1.462f},{.9930f,1.493f},{.9945f,1.664f},{.9955f,1.696f},{.9958f,1.792f},{.9962f,1.888f},{.9963f,1.947f},{.9963f,2.017f},{.9963f,2.032f},{.9966f,2.078f},{.9964f,2.144f}},
   {{.3943f,1.038f},{.5588f,0.992f},{.6718f,0.998f},{.7558f,1.023f},{.8236f,1.075f},{.8789f,1.195f},{.9249f,1.384f},{.9599f,1.626f},{.9788f,1.890f},{.9902f,2.478f},{.9956f,2.754f},{.9971f,2.978f},{.9977f,3.093f},{.9979f,3.210f},{.9981f,3.381f},{.9981f,3.483f},{.9981f,3.590f},{.9981f,3.648f},{.9982f,3.714f},{.9982f,3.780f}}
  },
  {	// B1=1M
   {{.3166f,0.575f},{.4831f,0.609f},{.6001f,0.620f},{.6880f,0.641f},{.7592f,0.682f},{.8175f,0.751f},{.8665f,0.868f},{.9057f,0.977f},{.9290f,1.087f},{.9495f,1.111f},{.9630f,1.203f},{.9704f,1.289f},{.9758f,1.318f},{.9783f,1.375f},{.9804f,1.448f},{.9814f,1.498f},{.9820f,1.551f},{.9825f,1.593f},{.9833f,1.630f},{.9835f,1.665f}},
   {{.3451f,2.039f},{.5099f,2.006f},{.6243f,2.031f},{.7098f,2.049f},{.7796f,2.143f},{.8367f,2.321f},{.8853f,2.611f},{.9247f,2.874f},{.9482f,3.178f},{.9677f,3.287f},{.9788f,3.545f},{.9849f,3.727f},{.9886f,3.622f},{.9904f,3.712f},{.9917f,3.866f},{.9924f,3.938f},{.9927f,3.995f},{.9930f,4.066f},{.9934f,4.143f},{.9934f,4.204f}},
   {{.3528f,4.524f},{.5139f,4.282f},{.6262f,4.219f},{.7107f,4.219f},{.7799f,4.351f},{.8368f,4.635f},{.8852f,5.167f},{.9253f,5.685f},{.9505f,6.233f},{.9727f,6.553f},{.9845f,7.263f},{.9904f,8.012f},{.9935f,8.266f},{.9948f,8.578f},{.9957f,8.888f},{.9961f,9.134f},{.9963f,9.310f},{.9965f,9.471f},{.9966f,9.666f},{.9966f,9.773f}},
   {{.3517f,9.653f},{.5101f,8.869f},{.6206f,8.636f},{.7046f,8.653f},{.7733f,8.822f},{.8299f,9.316f},{.8784f,10.235f},{.9194f,11.173f},{.9462f,12.209f},{.9712f,13.365f},{.9856f,15.739f},{.9930f,18.007f},{.9962f,18.714f},{.9974f,19.094f},{.9980f,20.363f},{.9983f,21.049f},{.9984f,21.483f},{.9984f,21.963f},{.9985f,22.460f},{.9985f,22.708f}}
  },
  {	// B1=10M
   {{.2769f,5.011f},{.4355f,5.072f},{.5490f,5.142f},{.6363f,5.197f},{.7081f,5.480f},{.7675f,5.825f},{.8185f,6.516f},{.8624f,7.148f},{.8918f,7.695f},{.9217f,7.752f},{.9418f,8.165f},{.9560f,8.562f},{.9669f,8.460f},{.9740f,8.439f},{.9792f,8.780f},{.9830f,8.855f},{.9857f,8.896f},{.9878f,8.973f},{.9891f,9.158f},{.9902f,9.231f}},
   {{.3068f,18.819f},{.4647f,18.112f},{.5762f,17.985f},{.6615f,18.046f},{.7314f,18.332f},{.7893f,19.055f},{.8391f,20.719f},{.8818f,22.331f},{.9110f,23.819f},{.9396f,24.654f},{.9584f,26.235f},{.9714f,28.119f},{.9801f,28.611f},{.9855f,29.088f},{.9892f,30.483f},{.9916f,31.252f},{.9932f,31.280f},{.9944f,30.612f},{.9951f,30.946f},{.9957f,31.207f}},
   {{.3166f,42.862f},{.4713f,39.684f},{.5807f,38.476f},{.6646f,38.043f},{.7336f,38.242f},{.7908f,39.507f},{.8401f,42.520f},{.8826f,45.619f},{.9125f,48.295f},{.9422f,51.705f},{.9623f,56.373f},{.9764f,62.327f},{.9853f,64.935f},{.9904f,68.325f},{.9935f,73.632f},{.9953f,76.263f},{.9964f,77.594f},{.9972f,80.353f},{.9976f,82.539f},{.9979f,83.649f}},
   {{.3177f,94.883f},{.4695f,83.028f},{.5773f,80.676f},{.6604f,79.604f},{.7287f,79.581f},{.7856f,82.471f},{.8348f,87.537f},{.8773f,93.251f},{.9078f,99.075f},{.9385f,107.330f},{.9602f,115.033f},{.9763f,134.631f},{.9867f,146.366f},{.9927f,161.787f},{.9960f,177.440f},{.9976f,192.664f},{.9984f,194.105f},{.9988f,199.850f},{.9991f,202.273f},{.9992f,211.193f}}
  },
  {	// B1=100M
   {{.2459f,45.715f},{.3961f,45.921f},{.5060f,47.616f},{.5919f,49.302f},{.6629f,50.739f},{.7223f,53.521f},{.7738f,57.470f},{.8188f,61.157f},{.8513f,64.017f},{.8852f,65.563f},{.9099f,67.837f},{.9296f,71.080f},{.9451f,71.079f},{.9562f,70.992f},{.9651f,74.488f},{.9717f,74.859f},{.9765f,75.637f},{.9804f,75.941f},{.9830f,78.553f},{.9852f,79.196f}},
   {{.2762f,182.322f},{.4273f,178.024f},{.5357f,170.808f},{.6197f,173.249f},{.6890f,171.695f},{.7469f,180.877f},{.7970f,185.983f},{.8406f,201.953f},{.8726f,201.556f},{.9052f,219.744f},{.9291f,231.662f},{.9482f,239.825f},{.9620f,256.314f},{.9717f,263.481f},{.9791f,278.170f},{.9840f,285.715f},{.9874f,291.255f},{.9900f,294.924f},{.9917f,303.282f},{.9930f,305.644f}},
   {{.2876f,465.948f},{.4357f,402.404f},{.5420f,387.975f},{.6247f,379.322f},{.6929f,380.389f},{.7500f,386.797f},{.7995f,404.830f},{.8427f,429.946f},{.8749f,444.951f},{.9077f,473.056f},{.9323f,503.093f},{.9524f,544.478f},{.9671f,574.930f},{.9773f,609.432f},{.9848f,660.495f},{.9894f,686.209f},{.9923f,714.367f},{.9942f,737.837f},{.9955f,768.149f},{.9964f,772.664f}},
   {{.2903f,951.426f},{.4357f,825.504f},{.5403f,778.724f},{.6221f,764.054f},{.6896f,762.494f},{.7463f,772.581f},{.7954f,802.854f},{.8384f,844.572f},{.8709f,866.212f},{.9041f,929.334f},{.9294f,992.735f},{.9506f,1081.799f},{.9665f,1161.895f},{.9780f,1264.091f},{.9867f,1397.161f},{.9919f,1519.190f},{.9949f,1630.192f},{.9968f,1729.262f},{.9978f,1831.371f},{.9984f,1878.709f}}
  }
 }
};

// From the data above we can now interpolate to get a pretty good approximation of pairing percentage and runtime.
// First we select using D's second missing prime (which governs the number of relocatables that have multiple relocations) to get down to 20 data sets.
// Then we interpolate using the B1 value, reducing from 20 data sets to 4.  Next we interpolate using B2 to get down to 1 data sets.
// Finally, we interpolate from the 20 relp_sets using totrels/numrels to arrive at our final pairing percentage and timing.

struct pair_data pair_interpolate (struct pair_data &a, struct pair_data &b, double min, double max, double val, int type)
{
	double log_fraction, linear_fraction;
	struct pair_data retval;

	// Cap B2 ratios that exceed the maximum
//GW: interpolation is poor for B2_ratios that exceed the 200 maximum.  Address this some day.
	if (type == 1 && val > max) val = max;

	// B1 and B2_ratio use a logarithmic "X-axis" (e.g. 10K,100K,1M or 20,50,100).  Compensate by taking the log of min, max, and val.
	// The multiplier "X-axis" is linear (e.g. 1,2,3,4).  No compensation necessary.
	if (type == 0 || type == 1) {
		min = log (min);
		max = log (max);
		val = log (val);
	}

	// Linear or logarithmic interpolation -- may not be the best/correct formula.  We should study it at some later date.
	// For now, the formulas seem decent enough.  But subtly wrong in that integral multipliers seem to be selected more often.
	log_fraction = (log(val) - log(min)) / (log(max) - log(min));
	linear_fraction = (val - min) / (max - min);

	// Interpolating B1 data is tricky.  Looking at multiplier=1, as B1 increases pair% decreases but not linearly.  However, looking at multiplier=7, as
	// B1 increases to 100K pair% increases and then decreases for B1 of 1M and higher.  Punt, choose linear interpolation until we come up with a better idea.
	// Interpolating B2 data is similarly tricky.  Sometimes pair% goes up or down as B2 ratio increases.
	// Interpolating multiplier data is less tricky.  Except for some benchmark anomalies, as multiplier increases pair% increases but less and less so.
	// Therefore interpolating multiplier should not be linear - skewed to giving more of the difference to the lower vals.

	// Interpolate and sanity check
	retval.pair_pct = (float) (a.pair_pct + (type <= 1 ? linear_fraction : log_fraction) * (b.pair_pct - a.pair_pct));
	retval.pair_runtime = (float) (a.pair_runtime + linear_fraction * (b.pair_runtime - a.pair_runtime));
	if (retval.pair_pct >= 1.0) retval.pair_pct = 0.9999f;
	if (retval.pair_runtime <= 0.0) retval.pair_runtime = 0.0001f;

	// Return result
	return (retval);
}

void estimate_pairing (
	int	second_missing_prime,
	uint64_t B1,
	uint64_t B2,
	int	totrels,
	int	numrels,
	float	*pair_pct,
	float	*timing)
{
	int	D_idx, B1_idx, B2_ratio_idx, multiplier_idx;
	double	B1_min, B1_max, B2_ratio, B2_ratio_min, B2_ratio_max, multiplier, multiplier_min, multiplier_max;
	struct pair_data a, b, c, d;

	ASSERTG (B1 > 0);

	// Select which D benchmark to use.  Different D benchmarks are needed because second-missing-prime affects the quantity of
	// multi-relocatable primes which in turn affects the pairing percentage.
	D_idx = (second_missing_prime == 11) ? 0 : (second_missing_prime == 13) ? 1 : 2;

	// Now select which B1 benchmark to use.  As B1 gets higher prime density drops as does pairing percentage.
	if (B1 < 100000) {
		B1_idx = 0;
		B1_min = 10000.0;
		B1_max = 100000.0;
	}
	else if (B1 < 1000000) {
		B1_idx = 1;
		B1_min = 100000.0;
		B1_max = 1000000.0;
	}
	else if (B1 < 10000000) {
		B1_idx = 2;
		B1_min = 1000000.0;
		B1_max = 10000000.0;
	}
	else {
		B1_idx = 3;
		B1_min = 10000000.0;
		B1_max = 100000000.0;
	}

	// Next we select which B2/B1 benchmark to use.  The B2/B1 ratio also affects number of relocatables and thus pairing percentage.
	B2_ratio = (double) B2 / (double) B1;
	if (B2_ratio < 50.0) {
		B2_ratio_idx = 0;
		B2_ratio_min = 20.0;
		B2_ratio_max = 50.0;
	}
	else if (B2_ratio < 100.0) {
		B2_ratio_idx = 1;
		B2_ratio_min = 50.0;
		B2_ratio_max = 100.0;
	}
	else {
		B2_ratio_idx = 2;
		B2_ratio_min = 100.0;
		B2_ratio_max = 200.0;
	}

	// Finally, select the correct benchmark for totrels/numrels ratio.  We only support totrels up to 20*numrels.
	multiplier = (double) totrels / (double) numrels;
	multiplier_idx = (int) floor (multiplier) - 1;
	if (multiplier_idx > 18) multiplier_idx = 18;
	multiplier_min = multiplier_idx + 1;
	multiplier_max = multiplier_idx + 2;

	// Now interpolate based on actual B1
	a = pair_interpolate (pair_data[D_idx][B1_idx][B2_ratio_idx][multiplier_idx], pair_data[D_idx][B1_idx+1][B2_ratio_idx][multiplier_idx], B1_min, B1_max, (double) B1, 0);
	b = pair_interpolate (pair_data[D_idx][B1_idx][B2_ratio_idx][multiplier_idx+1], pair_data[D_idx][B1_idx+1][B2_ratio_idx][multiplier_idx+1], B1_min, B1_max, (double) B1, 0);
	c = pair_interpolate (pair_data[D_idx][B1_idx][B2_ratio_idx+1][multiplier_idx], pair_data[D_idx][B1_idx+1][B2_ratio_idx+1][multiplier_idx], B1_min, B1_max, (double) B1, 0);
	d = pair_interpolate (pair_data[D_idx][B1_idx][B2_ratio_idx+1][multiplier_idx+1], pair_data[D_idx][B1_idx+1][B2_ratio_idx+1][multiplier_idx+1], B1_min, B1_max, (double) B1, 0);

	// Now interpolate based on actual B2/B1 ratio
	a = pair_interpolate (a, c, B2_ratio_min, B2_ratio_max, B2_ratio, 1);
	b = pair_interpolate (b, d, B2_ratio_min, B2_ratio_max, B2_ratio, 1);

	// Finally interpolate based on the actual totrels/numrels multiplier
	// ASSERTG (a.pair_pct <= b.pair_pct);  // There is some anamalous data for D=2310, multiplier 9 and 10
	a = pair_interpolate (a, b, multiplier_min, multiplier_max, multiplier, 2);

	// Return the pairing percentage and estimated time (on my quad core I5) to do the pairing
	*pair_pct = a.pair_pct;
	*timing = a.pair_runtime;
}

// Routine to estimate maximum number of Dsections that will fit in a maximum pair map size */

uint64_t estimate_max_pairmap_Dsections (
	double	max_pairmap_size,	/* Maximum size of the pairing map */
	uint64_t numDsections,		/* Number of Dsections */
	double	est_numpairs)		/* Estimated number of prime pairs plus singles */
{
	// Calculate the number of bytes we expect each D section will need in the pairing map (assumes each pairing requires one byte in the pairmap)
	double pairmap_bytes_per_Dsection = est_numpairs / (double) numDsections;
	// Compute the total number of D sections that will fit in the pairing map
	double max_numDsections = max_pairmap_size / pairmap_bytes_per_Dsection;
	// Return the maximum
	return ((uint64_t) ceil (max_numDsections));
}


/**********************************************************************************************************************/
/*                                                The pairing routines                                                */
/**********************************************************************************************************************/

// Turn on validation if debugging

#ifdef GDEBUG
#define VALIDATE
#endif

//GW: Need option to only return relocatables that are a multiple of relp???

// Sorted map to recycle and return relocatables as they are needed.  Key = base_prime * multiplier, value = base_prime.
using recycled_relocs =	std::map<uint64_t, uint64_t>;

class prime_generator {
public:
	// Constructor
	prime_generator (int thread_num, uint64_t first_relocatable, uint64_t last_relocatable, uint64_t B2_start_reloc, uint64_t B2_start,
			 uint64_t B2_end_prime, uint64_t B2_end_relocs, int first_multiplier, int second_multiplier);
	// Destructor
	~prime_generator () { /* GW: end_sieve?? */ }
	// Peek at the next prime
	uint64_t peek ();
	// Get the next "prime" as well small multiplier (one for a true prime)
	std::pair<uint64_t,uint64_t> next_prime () { uint64_t tmp = peek(); peek_value = 0; return {tmp, peek_base_prime}; }
	// Remove a relocatable from the recycleds pool
	void	remove_recycled (uint64_t p, uint64_t base_prime);
	// Return counts
	uint64_t get_numprimes () { return num_primes + num_relocatables; }
	uint64_t get_numrelocatables () { return num_relocatables; }
	// Routines to validate all primes are used and the prime pairings make sense
#ifdef VALIDATE
	void validate_insert (uint64_t p) { validation_map.insert(p); }
	void validate_prime (uint64_t p, uint64_t base_prime) {
		if (base_prime) p = base_prime;
		ASSERTG (validation_map.find (p) != validation_map.end());
		validation_map.erase (p);
	}
	void validate_pairing (uint64_t p1, uint64_t p2, int D, int ALT_D, int ALT_D_OFFSET, uint64_t B2_start, uint64_t B2_end, std::vector<int> &relps) {
		ASSERTG (std::binary_search (relps.begin(), relps.end(), labs ((long) (p1 - p2) / 2)));
		ASSERTG ((p1 + p2) / 2 > B2_start && (p1 + p2) / 2 < B2_end);
		if (ALT_D)
			ASSERTG (((p1 + p2) / 2) % D == 0 || ((p1 + p2) / 2) % ALT_D == ALT_D_OFFSET);
		else
			ASSERTG (((p1 + p2) / 2) % D == 0);
	}
	void validate_adjust (std::vector<int> &relps) {	// Look for relocatables that never should have been returned as a relocatable
		for (auto it = validation_map.begin (); it != validation_map.end (); ) {
			for (auto it2 = relps.begin (); ; ++it2) {
				if (it2 == relps.end () || *it * *it2 > B2) {
					it = validation_map.erase (it);
					num_relocatables--;
					break;
				}
				if (*it * *it2 > B2_start && *it * *it2 < B2) { ++it; break; }
			}
		}
	}
	void validate_final () {ASSERTG (validation_map.size() == 0);}
#else
	void validate_insert (uint64_t p) {}
	void validate_prime (uint64_t p, uint64_t base_prime) {}
	void validate_pairing (uint64_t p1, uint64_t p2, int D, int ALT_D, int ALT_D_OFFSET, uint64_t B2_start, uint64_t B2_end, std::vector<int> &relps) {}
	void validate_adjust (std::vector<int> &relps) {}
	void validate_final () {}
#endif

private:
	// Internal get next prime and relocatable
	void next_prime_from_sieve () { peek_prime = sieve (B2_sieve); if (peek_prime > B2) peek_prime = 0; else num_primes++, validate_insert (peek_prime); }
	void next_relocatable () {
		peek_relocated = sieve (relocatable_sieve);
		if (peek_relocated > last_reloc) peek_relocated = 0;
		else if (peek_relocated * first_multiplier < B2_start_reloc && peek_relocated * second_multiplier > B2_end_reloc) peek_relocated = 0;
		else num_relocatables++, validate_insert (peek_relocated);
	}
	void add_recycled (uint64_t p, uint64_t base_prime) {
		if (p >= B2_end_reloc) return;
		// Handle rare oddball that can occur when B2 > B1^2.
		if (recycleds_can_conflict && recycleds.find (p) != recycleds.end()) add_recycled (p + 2*base_prime, base_prime);
		else recycleds.insert ({p, base_prime});
	}
private:
	bool	recycleds_can_conflict;			// TRUE if there can be collisions in the recycleds map
	uint64_t B2_start_reloc;			// Start of the range of prime relocations to return (including 1x as a relocation!)
	uint64_t B2_start;				// Start of the range of primes to return
	uint64_t B2;					// End of the range of primes to return
	uint64_t B2_end_reloc;				// End of the range of prime relocations to return
	int	first_multiplier;			// First multiplier to apply to a relocatable prime
	int	second_multiplier;			// Second multiplier to apply to a relocatable prime

	void	*B2_sieve;				// Return primes from B2_start to B2
	void	*relocatable_sieve;			// Return primes from B1 to B2_start
	recycled_relocs recycleds;			// Sorted map of future relocatables to return as needed
	uint64_t last_reloc;				// Last relocatable prime to be returned by the relocatable sieve

	uint64_t peek_value;				// The minimum of peek_prime, peek_relocated, peek_recycled (i.e. next "prime" to return)
	uint64_t peek_base_prime;			// If peek_value is composite (relocated), this is the base prime

	uint64_t peek_prime;				// Next prime from the B2_sieve
	uint64_t peek_relocated;			// Next relocatable prime that requires a multiplier of 3

	uint32_t num_primes;				// Total number of primes (excluding relocatables)
	uint32_t num_relocatables;			// Total number of relocatable primes
#ifdef VALIDATE
	std::unordered_set<uint64_t> validation_map;
#endif
};

// Prime generator constructor
prime_generator::prime_generator (
	int	thread_num,
	uint64_t first_relocatable,
	uint64_t last_relocatable,
	uint64_t B2_start_reloc,			// Return no relocated primes below this value
	uint64_t B2_start,				// Return no primes below this value
	uint64_t B2_end_prime,				// The traditional bound #2 value, return no primes larger than this value
	uint64_t B2_end_reloc,				// Since the last D blocks likely end after B2, we can put relocated primes after B2 in the last block.
							// Return no more relocated primes after this value.
	int	first_multiplier,			// First multiplier to apply to a relocatable prime
	int	second_multiplier)			// Second multiplier to apply to a relocatable prime
{
	if (B2_start_reloc < first_relocatable) B2_start_reloc = first_relocatable;
	this->B2_start_reloc = B2_start_reloc;
	this->B2_start = B2_start;
	this->B2 = B2_end_prime;
	this->B2_end_reloc = B2_end_reloc;
	this->first_multiplier = first_multiplier;
	if (second_multiplier > first_multiplier * first_multiplier) second_multiplier = first_multiplier * first_multiplier;
	this->second_multiplier = second_multiplier;

	recycleds_can_conflict = (first_relocatable * first_relocatable <= B2_end_reloc);

	num_primes = 0;
	num_relocatables = 0;

	peek_value = 0;
	peek_base_prime = 0;

	// Start the two sieves
	B2_sieve = NULL;
	relocatable_sieve = NULL;
	// Sometimes start the primes sieve a little early so that we can place a few of the last relocatables at 1 * base_prime
	if (ONE_TIMES_BASE && last_relocatable == B2_start) {
		start_sieve_with_limit (thread_num, first_relocatable, (uint32_t) sqrt((double) B2_start_reloc), &relocatable_sieve);
		start_sieve_with_limit (thread_num, B2_start_reloc, (uint32_t) sqrt((double) B2), &B2_sieve);
		last_reloc = B2_start_reloc - 1;
	} else {
		start_sieve_with_limit (thread_num, first_relocatable, (uint32_t) sqrt((double) last_relocatable), &relocatable_sieve);
		start_sieve_with_limit (thread_num, B2_start, (uint32_t) sqrt((double) B2), &B2_sieve);
		last_reloc = last_relocatable;
	}
	next_prime_from_sieve ();
	next_relocatable ();

	// The first few primes from the prime sieve may really be relocatables we want to offer up as 1 * base_prime
	while (peek_prime && peek_prime < B2_start) {
		add_recycled (peek_prime, peek_prime);
		num_primes--; num_relocatables++;
		next_prime_from_sieve ();
	}

	// Primes from the relocatable sieve that require a multiplier above 3 must be moved to the recycleds map.
	// It is possible for a relocatable to have no valid relocations!  For example, B1=1M, B2_start=30M, B2=210M:  Suppose a bitmap
	// is generated and processed up to B2_done=180M when the job is interrupted. On restart with less availble memory, the bitmap
	// may need to be thrown away and fill_pairmap is called with first_reloc=1M, last_reloc=30M, B2_start=180M, B2=210M.  Not all
	// primes between 1M and 30M can be relocated to 180M to 210M.
	while (peek_relocated && peek_relocated * first_multiplier < B2_start_reloc) {
		// Recycle the relocatable prime to its first valid location
		uint32_t multiplier = (uint32_t) ((B2_start_reloc / peek_relocated) + 1) | 1;
		if (multiplier < (uint32_t) second_multiplier) multiplier = second_multiplier;
		add_recycled (peek_relocated * multiplier, peek_relocated);
		// Get next relocatable until relocatable prime * first_multiplier is above B2_start_reloc
		next_relocatable ();
	}
}

// Take a peek at next "prime" to be returned by next_prime
uint64_t prime_generator::peek ()
{
	if (peek_value == 0) {
		// Handle returning next result from the B2_sieve
		if (peek_prime &&
		    (peek_relocated == 0 || peek_prime < peek_relocated * first_multiplier) &&
		    (recycleds.empty () || peek_prime < recycleds.begin()->first)) {
			peek_value = peek_prime;
			peek_base_prime = 0;
			next_prime_from_sieve ();
		}
		// Handle returning next result from the relocatable_sieve
		else if (peek_relocated && (recycleds.empty () || peek_relocated * first_multiplier < recycleds.begin()->first)) {
			peek_value = peek_relocated * first_multiplier;
			peek_base_prime = peek_relocated;
			add_recycled (peek_relocated * (first_multiplier + 2), peek_base_prime);
			next_relocatable ();
		}
		// Handle returning next result from recycled relocatables
		else if (!recycleds.empty ()) {
			peek_value = recycleds.begin()->first;
			peek_base_prime = recycleds.begin()->second;
			recycleds.erase (recycleds.begin());
			add_recycled (peek_value + 2 * peek_base_prime, peek_base_prime);
		}
	}
	return (peek_value);
}

// Return an unused relocatable to the pool
void prime_generator::remove_recycled (
	uint64_t p,
	uint64_t base_prime)
{
	uint64_t next_prime;
	uint32_t next_multiplier;

	ASSERTG (p % base_prime == 0);

	// Hunt for the recycled entry for this base prime so that we can delete it
	for (next_multiplier = (uint32_t) (p / base_prime) + 2; ; next_multiplier += 2) {
		next_prime = base_prime * next_multiplier;
		if (next_prime >= B2_end_reloc) break;
		if (next_prime == peek_value) { peek_value = 0; peek_base_prime = 0; }
		auto it = recycleds.find (next_prime);
		if (it != recycleds.end() && it->second == base_prime) { recycleds.erase (it); break; }
	}
}

void output_pair (
	uint64_t p1,
	uint64_t p2,
	int	D,
	int	ALT_D,
	int	ALT_D_OFFSET,
	uint64_t B2_start,		// First D section
	int	totrels,
	std::vector<int> &relps,
	std::set<uint64_t> &pairmap_data)
{
	uint64_t  Dblock;
	int	relp;
	ASSERTG (ALT_D == 0);  // Not supported yet
	Dblock = (p1 + p2) / 2;
	relp = abs ((int) (p1 - Dblock));
	Dblock = (Dblock - (B2_start + D / 2)) / D;
	relp = (int) (std::lower_bound (relps.begin(), relps.end(), relp) - relps.begin());
	pairmap_data.insert (Dblock * totrels + relp);
}

int fill_pairmap (			/* Generate a pairing map */
	int	thread_num,		/* For outputting informative messages */
	void	**sieve_info,		/* Prime number sieve to use / initialize */
	int	D,			/* Calculated by best_stage2_impl, best D value ("big step") */
	int	WINDOW_SIZE,		/* Size (in number of D values) of the prime window.  Use zero for non-windowed pairing (slower but optimal). */
	int	ALT_D,			/* Calculated by best_stage2_impl, best additional D value (if any) */
	int	ALT_D_OFFSET,		/* Calculated by best_stage2_impl, best additional D value "offset" (if any) */
	int	ALT_MORE_RELPS,		/* Calculated by best_stage2_impl, include relative primes divisible by this value in support of additional D value */
	int	totrels,		/* Calculated by best_stage2_impl, number of relative primes to use for pairing */
	int16_t	*relp_sets,		/* Which non-contiguous sets of relative primes to use */		
	uint64_t first_relocatable,	/* First relocatable prime (same as B1 unless pairmap must be split or mem change caused a replan) */
	uint64_t last_relocatable,	/* End of relocatable primes (same as B2_start unless a change in available memory caused a new stage 2 plan) */
	uint64_t B2_start,		/* First D section to place in the pairmap */
	uint64_t B2,			/* Bound #2, end sieving for primes here */
	uint64_t max_pairmap_Dsections,	/* Number of D sections that can fit in a pairing map */
	uint8_t	**pairmap,		/* Returned pointer to a pairing map that is allocated here */
	uint64_t *pairmap_size)		/* Returned size of the pairing map */
{
	int	numrels;		/* Number of relative primes less than D/2 */
	uint64_t B2_end;		/* D section just after the last D section */
	uint64_t B2_start_reloc;	/* The first relocated prime reachable from the first D block */
	uint64_t B2_end_reloc;		/* The last relocated prime reachable from the last D block */
	uint64_t numDsections;		/* Number of D sections to output to the pairmap(s) */
	std::set<uint64_t> pairmap_data; /* Pairing data used to build pairmap */
	uint8_t *pairmap_ptr;		/* Pointer to next pairmap byte to output */
	int32_t last_pair_output;	/* Last pair/single output to the pairmap */
	int	tot_pairs, tot_singles, reloc_singles;
	int	i, int_multiplier, max_relp;
	std::vector<int> relps;		/* A set of all possible positive relative primes */
	bool	WINDOWED;		/* True if faster prime window will be used, reduces prime pairing a little bit */
	int	WINDOW_REDUCTION_SIZE;	/* Number of D rows to process in the prime window */

/* Do some prep work for windowed vs. non-windowed pairing */

	if (WINDOW_SIZE) {
		WINDOWED = TRUE;			// This used to be a #define, which is why it in caps
		WINDOW_REDUCTION_SIZE = 1;		// We originally tried processing half the window at a time.  This was better.
	} else {
		WINDOWED = FALSE;			// All primes and relocs are put into one window -- no matter how large that might be
		WINDOW_REDUCTION_SIZE = 0;		// All rows are processed in the prime window are processed
	}

/* Sanitize inputs */

	if (first_relocatable > last_relocatable) first_relocatable = last_relocatable;
	if (first_relocatable > B2_start) first_relocatable = B2_start;
	if (last_relocatable > B2_start) last_relocatable = B2_start;

/* Calculate the relative primes less than D/2 */
/* When using alternate D's we allow including the relative primes for alternate offset */

	if (ALT_D == 0 || ALT_MORE_RELPS == 0) {
		for (i = 1; i < D / 2; i += 2) if (_intgcd (i, D) == 1) relps.push_back (i);
	} else {
		ASSERTG (ALT_MORE_RELPS > 1);
		if (D % ALT_MORE_RELPS != 0) for (i = 1; i < D / 2; i += 2) { if (_intgcd (i, D) == 1) relps.push_back (i); }
		else for (i = 1; i < D / 2; i += 2) { if (_intgcd (i, D / ALT_MORE_RELPS) == 1) relps.push_back (i); }
	}
	numrels = (int) relps.size();

/* Compute the multiplier which is the possibly fractional number of sets of numrels */

	int_multiplier = divide_rounding_up (totrels, numrels);
	ASSERTG (int_multiplier <= 30);

/* Calculate the maximum relative prime (handles relp_sets that are not sorted) */

	max_relp = 0;
	for (i = 0; i < int_multiplier; i++) {
		bool	last_iteration = (i == int_multiplier - 1);
		int	this_set_max_relp;
		if (relp_sets[i] >= 0) {
			this_set_max_relp = relp_sets[i] * D;
			this_set_max_relp += !last_iteration ? relps[numrels-1] : relps[(totrels - 1) % numrels];
		} else {
			this_set_max_relp = -relp_sets[i] * D;
			this_set_max_relp -= !last_iteration ? relps[0] : relps[(numrels - totrels % numrels) % numrels];
		}
		if (this_set_max_relp > max_relp) max_relp = this_set_max_relp;
	}

/* Calculate the additional, possibly non-contiguous, positive relative primes */

	ASSERTG (relp_sets[0] == 0);	// Skip set zero, we've calculated those already
	for (i = 1; i < int_multiplier; i++) {
		for (int j = 0; j < numrels; j++) {
			int	relp;
			if (relp_sets[i] >= 0) relp = relp_sets[i] * D + relps[j];
			else relp = - relp_sets[i] * D - relps[numrels - 1 - j];
			relps.push_back (relp);
			if (relps.size() == totrels) break;
		}
	}
	std::sort (relps.begin(), relps.end());

/* Initialize the pairing distances */

	using distances = std::vector<int64_t>;		// An array of distances to possible prime pairings
	std::map<int,distances> relp_distances;		// For each relp remember a vector of distances to primes we can pair with
	std::map<int,distances> alt_relp_distances;	// For each alt_relp remember a vector of distances to primes we can pair with

	// Calculate the possible distances for each relative prime
	for (auto && relp : relps) {
		int base_relp = (relp + D/2) % D - D/2;
		relp_distances[-base_relp].push_back (2*relp);
		relp_distances[base_relp].push_back (-2*relp);
	}

	// Calculate the possible distances for each alternate relative prime
	if (ALT_D)
	for (auto && relp : relps) {
		int base_relp = (relp + ALT_D/2) % ALT_D - ALT_D/2;
		alt_relp_distances[-base_relp].push_back (2*relp);
		alt_relp_distances[base_relp].push_back (-2*relp);
	}

/* Figure out how many D sections will be in this pairmap, adjust B2_end and C if we're only processing some of the primes (a split pairmap) */

	B2_start = round_down_to_multiple_of (B2_start - D / 2, D) + D / 2;
	B2_end = round_up_to_multiple_of (B2 + D / 2, D) - D / 2;
	if (max_relp > B2_start + D / 2) B2_start_reloc = 0;
	else B2_start_reloc = B2_start + D / 2 - max_relp;
	if (B2_start_reloc < first_relocatable) B2_start_reloc = first_relocatable;
	numDsections = (B2_end - B2_start) / D;
	if (numDsections > max_pairmap_Dsections) {
		numDsections = max_pairmap_Dsections;
		B2_end = B2 = B2_start + numDsections * D;
		// Adjust last_relocatable down when maxDsections is forcing us to split the pairmap.  B2_end will be the next pairmap's B2_start.
		// Adjusted last_relocatable will be the next first_relocatable.
		uint64_t next_first_relocatable = B2_end / relps[1];
		if (last_relocatable > next_first_relocatable) last_relocatable = next_first_relocatable;
		if (last_relocatable < first_relocatable) last_relocatable = first_relocatable;
	}
	B2_end_reloc = B2_end - D / 2 + max_relp;
	ASSERTG (first_relocatable == last_relocatable || (last_relocatable * relps[1] >= B2_start && last_relocatable * relps[1] <= B2_end));

/* Init class to generate each prime (or more than one for relocatables) between B2_start and B2_end. */

//GW: share the sieve_info
	prime_generator pgen (thread_num, first_relocatable, last_relocatable, B2_start_reloc, B2_start, B2, B2_end_reloc, relps[1], relps[2]);

/* Initialize the prime window */

	// Data is added in ascending order resulting in a sorted list.  I elected to use a circular buffer rather than a list to
	// save on repeated allocates and frees.  I also create a custom unordered map for O(1) access.
	// I originally used an std::map, but O(log n) lookups and deletes with frequent memory allocations were a performance issue.
	class prime_data;
	using prime_window = boost::circular_buffer<prime_data>;
	using relocs_in_prime_window = std::unordered_multimap<uint64_t, prime_data*>;
	using used_relocs_in_prime_window = std::unordered_map<uint64_t, prime_data*>;

	class prime_data {
	public:
		// Is prime already paired?
		bool is_paired() { return (p_paired_with != NULL); }
		// Set, test, get a backlink
		void set_backlink (uint32_t backlink_id, prime_data* backlink) { this->backlink_id = backlink_id; this->backlink = backlink; }
		bool has_backlink (uint32_t backlink_id) { return (this->backlink_id == backlink_id); }
		prime_data* get_backlink () { return (backlink); }
		// Score a relocatable.  The higher the score, the more desirable it is to pair it.  A lower score means the relocatable is more "flexible".
		int score_reloc () { if (must_pair_or_single) return (1); if (last_prime_window) return (0); return (-1); }
		// Compare two prime datas
		bool operator< (const prime_data &other) { return (this->prime < other.prime); }
	public:
		uint64_t prime;				// The prime value or relocated prime value.
		uint64_t base_prime;			// Zero for a true prime, base prime for a relocated "prime"
		int16_t	relp;				// Relative prime to D (between -D/2 and D/2)
		int16_t	alt_relp;			// When using an additional D (giant step), this is the relp for the alternate D
#ifndef ON_THE_FLY_CAN_PAIR_TO
		uint32_t first_can_pair_to;		// First in a list of primes this prime can pair to
		uint8_t	num_can_pair_to;		// Number of primes in the prime window this prime can possibly pair to
#endif
		bool	is_valid;			// True is prime is valid (when prime is paired and output it is marked invalid).
							// We do not erase the prime as that might invalidate iterators.
		bool	must_pair_or_single;		// True if prime must be dealt with this prime window.  Relocatables that can
							// appear in future prime windows will not set this.
		bool	last_prime_window;		// True if relocatable prime will not appear in any future prime windows.
		uint32_t backlink_id;			// Counter to determine the validity of a backlink
		prime_data *backlink;			// Back pointer that lets us unravel a chain of pairings.
		prime_data *p_paired_with;		// Pointer to a prime in the prime window we are tentatively paired with
	};
	// Compare function that compares two prime_data objects
	struct prime_data_cmp {
		bool operator() (const prime_data &lhs, const prime_data &rhs) const { return (lhs.prime < rhs.prime); }
		bool operator() (const prime_data &lhs, uint64_t rhs) const { return (lhs.prime < rhs); }
		bool operator() (uint64_t lhs, const prime_data &rhs) const { return (lhs < rhs.prime); }
	};

	// Brutally fast mapping of a prime to an index into prime_window
	class prime_map {
	public:
		const uint32_t NOT_IN_PRIME_MAP = 0xFFFFFFFF;
		// Constructor
		prime_map (uint32_t window_size) {
			prime_count = 0; base_prime_count = 0;
			mask = next_pow2 (window_size / 2) - 1;
			map.resize (mask + 1, NOT_IN_PRIME_MAP);
		}
		// Insert prime into the map
		void insert (uint64_t prime) { map[(uint32_t)prime/2 & mask] = prime_count++; prime_count &= mask; }
		void faux_insert (uint64_t prime) { prime_count++; prime_count &= mask; }
		// Find index into prime_window circular buffer.  Returns NOT_IN_PRIME_MAP if value is not in the prime window.
		uint32_t find (uint64_t value) {
			auto x = map[(uint32_t)value/2 & mask];
			if (x == NOT_IN_PRIME_MAP) return (NOT_IN_PRIME_MAP);
			return ((x - base_prime_count) & mask);
		}
		// Erase prime from the map
		void erase (uint64_t prime) { map[(uint32_t)prime/2 & mask] = NOT_IN_PRIME_MAP; }
		// Pop the front prime from the prime map
		void pop_front (uint64_t prime) { erase (prime); base_prime_count++; }
	private:
		uint32_t prime_count;		// Count of primes inserted into the map
		uint32_t base_prime_count;	// Count of the primes removed from the front of the map
		uint32_t mask;			// Mask to create a circular index into the map vector
		std::vector<uint32_t> map;	// Map to an index into the prime_window circular buffer
	};

	// Each prime in the prime window needs a small array of pointers to other primes it can possibly pair with.  We gather these
	// small arrays into one large array here for memory efficiency.  Conceptually, this is a circular buffer so that these small
	// arrays never need to be freed.  Even better, we assure that every small array is stored contiguously, so that an 32-bit index
	// into this array can be saved by each prime in the prime array rather than a 64-bit pointer.
	class can_pair_to_links {
	public:
		// Constructor
		can_pair_to_links (uint32_t max_size) {
			next_available = 0;
			this->max_size = max_size;
			map.reserve (max_size);
		}
		// Insert prime into the map
		uint32_t start_small_array () { if (next_available > max_size - 100) next_available = 0; return (next_available); }
		// Insert prime into the map
		void insert (prime_data *p) { map[next_available++] = p; }
		// Get data from the small array
		prime_data *operator[](int idx) { return (map[idx]); }
	private:
		uint32_t	next_available;		// Next available entry in the circular buffer
		uint32_t	max_size;		// Maximum entries in the circular buffer
		std::vector<prime_data*> map;		// Circular buffer of pointers to primes in the prime window
	};

//GW:Should create a prime_window class (superclass?) that combines primes and primes_map into one class -- the two must operate in lock step!

	int	capacity, window_size, cpt_links, max_relocs_per_window;
	double est_numprimes_relocated = primes_less_than (last_relocatable) - primes_less_than (first_relocatable);
	if (WINDOWED) {
		// First window will have the highest prime density
		window_size = WINDOW_SIZE * D;
		double est_numprimes = primes_less_than (B2_start + window_size) - primes_less_than (B2_start);
		// 20% safety margin
		est_numprimes *= 1.2;
		// Limit the number of relocs.  Needed for the first window when B2 is much much greater than B1.  In that case, there will
		// 1x, 11x, 13x, 17x, etc. relocs available which will swamp our prime window capacity.  This reloc limit will allow for
		// up to one reloc for every expected prime.
		max_relocs_per_window = (int) est_numprimes;
		est_numprimes *= 2.0;			// Estimate of number of primes and number of relocs in the window
		double est_density = est_numprimes / (double) window_size;
		double est_cpt_density = est_density * ((double) (D/2) / (double) numrels);
		capacity = (int) ((double) window_size * est_density);
		cpt_links = (int) (capacity * (double) totrels / (double) numrels * est_cpt_density);
	} else {
		double est_numprimes = primes_less_than (B2_end) - primes_less_than (B2_start);
		// My theoretical calculations for D=2310,A=1,B2/B1=133 say there should be an average of 5.18 relocations, plus an adjustment for
		// relocations below B2_start, plus an adjustment for relocations above B2_end.  Actual data shows 6.17 average relocs.  If A=11,
		// there are 7.14 average relocs.
		est_numprimes += 5.14 * est_numprimes_relocated * (double) B2_start / (double) B2_start_reloc * (double) B2_end_reloc / (double) B2_end;
		if (ALT_D) est_numprimes += est_numprimes_relocated;
		double est_density = est_numprimes / ((double) numDsections * (double) D);
		double est_cpt_density = est_density * ((double) (D/2) / (double) numrels);
		window_size = (int) (B2_end_reloc - B2_start_reloc);
		capacity = (int) (est_numprimes * 1.2);					// 20% margin of error
		cpt_links = (int) (capacity * (double) totrels / (double) numrels * est_cpt_density);
	}
	if (ALT_D) cpt_links *= 2;

	prime_window primes(capacity);			// For each prime in the "pairing window", maintain data for prime pairing
	prime_map primes_map(window_size);		// Create a map for quick lookup of primes in the prime window
#ifndef ON_THE_FLY_CAN_PAIR_TO
	can_pair_to_links can_pair_to(cpt_links);	// Keep track of which primes a prime can pair to
#endif
	relocs_in_prime_window relocs;			// Keep track of which <base prime, relocated prime> are in the "prime pairing window"
	used_relocs_in_prime_window used_relocs;	// Keep track of the <base prime, relocated prime>s that are tentatively paired
	uint64_t prime_window_base;			/* Smallest possible prime in the primes window */
	uint32_t backlink_id;				/* Counter to validate backlinks -- should be hidden in a prime_window class */
#define next_backlink_id() { backlink_id++; if (backlink_id == 0) backlink_id = 1; }

/* Allocate the pairmap.  We guess at the size and grow/shrink as necessary, so our guess does not need to be great. */

	{
		float	pair_pct, timing;
		double	est_numprimes;
		estimate_pairing (11, first_relocatable, B2_end, totrels, numrels, &pair_pct, &timing);
		est_numprimes = primes_less_than (B2_end) - primes_less_than (B2_start);
		est_numprimes += primes_less_than (last_relocatable) - primes_less_than (first_relocatable);
		*pairmap_size = (uint64_t) (est_numprimes * (1.0 - pair_pct / 2) * 1.1);	// An extra 10% for good measure
		*pairmap = (uint8_t *) realloc (*pairmap, (size_t) *pairmap_size);
		if (*pairmap == NULL) goto oom;
	}
	pairmap_data.insert (numDsections * totrels);		// Insert pairmap terminator (first relp after last D section)
	pairmap_ptr = *pairmap;
	last_pair_output = -1;

/* Work through all the primes one window at a time.  A prime window is a large collection of primes where we maximize the pairing within the window. */

	reloc_singles = 0;
	tot_pairs = 0;
	tot_singles = 0;
	prime_window_base = B2_start;
	backlink_id = 0;
	for ( ; ; ) {
		uint64_t next_prime;
		uint64_t first_prime = pgen.peek ();
		int	relocs_added = 0;
		double reloc_fill_rate;

		// Figure out how fast we can add relocatables without exceeeding the maximum
		if (WINDOWED) {
			if (first_prime < prime_window_base) reloc_fill_rate = (double) max_relocs_per_window / (WINDOW_SIZE * D + prime_window_base - first_prime);
			else reloc_fill_rate = (double) (max_relocs_per_window + (max_relocs_per_window - relocs.size())) / (WINDOW_SIZE * D);
		}

		// Fill the prime window.  After loading the last row, load all the relocatables above b2_end
		uint64_t next_prime_window_base = WINDOWED ? prime_window_base + WINDOW_SIZE * D : B2_end;
		while ((next_prime = pgen.peek ()) != 0 && (next_prime < next_prime_window_base || next_prime_window_base >= B2_end)) {
			uint64_t prime, base_prime;
			uint64_t D_block;
			int16_t	remainder, alt_remainder;
			bool	must_pair, last_window;

			// Get next prime
//GW is there a way to use auto here?
			std::tie (prime, base_prime) = pgen.next_prime ();

			// Split prime into D-block and remainder where remainder is between -D/2 and D/2
			D_block = (prime + D / 2) / D;
			remainder = (int16_t) ((int64_t) prime - (int64_t) (D_block * D));
			if (! std::binary_search (relps.begin(), relps.begin() + numrels, abs (remainder))) remainder = 0;
//GW: Can remainder ever by 0 (not in relps)??   Yes - 3*reloc, 5*reloc, etc.  have pgen weed them out?
// work goes into creating and weeding out these useless relocs

			if (ALT_D) {
				uint64_t alt_D_block;
				alt_D_block = ((prime - ALT_D_OFFSET) + ALT_D / 2) / ALT_D;
				alt_remainder = (int16_t) ((int64_t) (prime - ALT_D_OFFSET) - (int64_t) (alt_D_block * ALT_D));
//GW: Can alt_remainder ever by 0 if ALT_MORE_RELPS is set?
				if (! std::binary_search (relps.begin(), relps.begin() + numrels, abs (alt_remainder))) alt_remainder = 0;
			} else
				alt_remainder = 0;

			// True primes must be paired or output as singles while in this prime window.
			if (base_prime == 0) {
				ASSERTG (remainder != 0 || alt_remainder != 0);
				must_pair = TRUE;
				last_window = FALSE;
			}

			// Relocated primes that are not represented by a relp or alt_relp cannot pair, ignore them
			else if (remainder == 0 && alt_remainder == 0) continue;

			// Relocated primes that do not have any possible legal pairings can be ignored
			else if (prime < B2_start &&
				 (relp_distances[remainder].front() < 0 || (prime + (prime + relp_distances[remainder].front())) / 2 < B2_start) &&
				 (relp_distances[remainder].back() < 0 || (prime + (prime + relp_distances[remainder].back())) / 2 < B2_start)) continue;
			else if (prime > B2_end &&
				 (relp_distances[remainder].front() > 0 || (prime + (prime + relp_distances[remainder].front())) / 2 > B2_end) &&
				 (relp_distances[remainder].back() > 0 || (prime + (prime + relp_distances[remainder].back())) / 2 > B2_end)) continue;

			// Relocatable primes when WINDOWing is not set are easy.  Don't set must_pair, do set last_window.
			else if (!WINDOWED) {
				must_pair = FALSE;
				last_window = TRUE;
			}

			// Relocated primes when windowing must be handled carefully.  We need to know if this is the last prime window the
			// relocatable will appear in (must pair it) and also deal with case where the relocatable may appear more than once
			// within this prime window.
			else {
				bool	have_base_primes_in_window;

				// Set flag if we already have base primes represented in this prime window
				have_base_primes_in_window = (relocs.find (base_prime) != relocs.end());

				// Gather data on the next relocation of the base prime
				for (i = (int) (prime / base_prime) + 2; ; i += 2) {
					int	j = i % D;
					if (j > D / 2) j = D - j;
					if (std::binary_search (relps.begin(), relps.begin() + numrels, j)) break;
				}
				uint64_t next_relocation = base_prime * i;
				uint64_t next_D_block = (next_relocation + D / 2) / D;
				int next_remainder = (int) ((int64_t) next_relocation - (int64_t) (next_D_block * D));

				// If this is a 1x relocation (below B2_start), then we can't set last_window as we're not allowed to output it as a single.
				if (prime == base_prime) last_window = FALSE;
				// If next relocation is well before B2_end then set flag indicating this is not the last
				// window the relocatable prime appears in.  Be wary of max_relp exceeding B2_end.
				else if ((int64_t) next_relocation < (int64_t) B2_end - (int64_t) max_relp) last_window = FALSE;
				// If the next relocation is way past B2_end it would be useless for pairing, set flag indicating this is
				// the last relocation of the base prime.
				else if (next_relocation >= B2_end_reloc) last_window = TRUE;
				// Here we assume that most values in relp_sets are positive.  When this is the case, positive remainders pair
				// backwards providing the maximum number of pairing opportunities.  Set flag indicating this is not the last
				// window the relocatable prime appears in as long as it isn't relocated "too far" past B2_end (doesn't lose too
				// many of it's prime pairing opportunities).
				else if (next_remainder > 0) last_window = (next_relocation > B2_end + max_relp/6);
				// Negative remainders pair in the forwards direction.  We find that we get more total pairing when we ignore
				// most of these possible relocations.  Set flag indicating this is the last window the relocatable prime appears
				// in if "too many" of the possible pairings will be past past B2_end.
				else last_window = ((int64_t) next_relocation > (int64_t) B2_end - (int64_t) max_relp + (int64_t) max_relp/32);

				// Ignore excessive relocatables.  Allow first 10 relocs without question, then apply the rate limit.  Probably too paranoid,
				// but I worry about an aberrant "clump" of relocs right at the start of the window.
				if (WINDOWED && !last_window && prime_window_base + WINDOW_SIZE * D < B2_end &&
				    relocs_added >= 10 && relocs_added > (prime - first_prime) * reloc_fill_rate) continue;
				relocs_added++;

				// Set must_pair if we are certain the relocatable is only in the primes window once
				// and this is the last prime window this relocatable will appear in.
				must_pair = !have_base_primes_in_window && last_window;
				// If last_window is set, make sure the prime generator does not spit out more multiples of the base prime.
				if (last_window && (prime + 2 * base_prime < B2_end_reloc)) pgen.remove_recycled (prime, base_prime);
			}

			// Add prime and its data to the primes window (use hint to insert before the end)
#ifndef ON_THE_FLY_CAN_PAIR_TO
			auto it = primes.insert (primes.end(), {prime, base_prime, remainder, alt_remainder, 0, 0, TRUE, must_pair, last_window, 0, NULL, NULL});
#else
			auto it = primes.insert (primes.end(), {prime, base_prime, remainder, alt_remainder, TRUE, must_pair, last_window, 0, NULL, NULL});
#endif
			// Don't insert primes that are below the prime_window_base into the map.  This only happens for relocatables below B2_start.
			if (prime >= prime_window_base) primes_map.insert (prime);
			else primes_map.faux_insert (prime);

			// Insert in the relocatable map
			if (!it->must_pair_or_single) relocs.insert ({base_prime, &*it});
		}

		// Make sure circular buffer is not full
		ASSERTG (primes.size () != capacity);

#ifndef ON_THE_FLY_CAN_PAIR_TO
		// Build a map of every prime that a prime can pair to
		int num_cpt_links = 0;
		for (auto it = primes.begin(); it != primes.end(); ++it) {
			if (!it->is_valid) continue;
			it->num_can_pair_to = 0;
			it->first_can_pair_to = can_pair_to.start_small_array();
			for (int link_type : {0,1}) {
				distances *d;
				if (link_type == 0) {
					if (!it->relp) continue;
					d = &relp_distances[it->relp];
				} else {
					if (!it->alt_relp) continue;
					d = &alt_relp_distances[it->alt_relp];
				}
				// Examine each possible distance
				for (auto && dist : *d) {
					uint64_t next_in_chain = it->prime + dist;
					// Ignore possible nexts above the end of the prime window
					if (WINDOWED && next_in_chain > prime_window_base + WINDOW_SIZE * D) continue;
					// Make sure the next in chain is generated using a D value that is between B2_start and B2_end
					if ((it->prime + next_in_chain) / 2 < B2_start || (it->prime + next_in_chain) / 2 >= B2_end) continue;
					// We can't safely use the fast prime_map for primes below prime_window_base.
					if (next_in_chain < prime_window_base) {
						if (prime_window_base != B2_start) continue;
						auto itfind = std::lower_bound (primes.begin(), primes.end(), next_in_chain, prime_data_cmp());
						if (itfind->prime != next_in_chain) continue;
						can_pair_to.insert (&*itfind);
					}
					// Finally, make sure the next in chain is part of this primes window
					else {
						auto primes_index = primes_map.find (next_in_chain);
						if (primes_index == primes_map.NOT_IN_PRIME_MAP) continue;
						auto it_next_in_chain = primes.begin() + primes_index;
						ASSERTG (it_next_in_chain->prime == next_in_chain);
						can_pair_to.insert (&*it_next_in_chain);
					}
					it->num_can_pair_to++;
					num_cpt_links++;
				}
			}
		}
		// Make sure we do not overflow cpt_links buffer
		ASSERTG (num_cpt_links <= cpt_links);
#endif

// Look for augmenting paths from unpaired primes to other unpaired relocatables or primes.
// Note we never start a route from a relocatable prime.  We do not want to pair a relocatable to another relocatable.
// Furthermore, when links are rearranged we must detect any attempt to create a relocatable-to-relocatable link.

		for (int pass = 1; pass <= MAX_PASSES; pass++) {

		    // Start at first row unless WINDOW_REDUCTION_SIZE is one where we start at the middle row
		    prime_window::iterator it;
		    if (WINDOW_REDUCTION_SIZE != 1 || prime_window_base == B2_start) it = primes.begin();
		    else it = std::lower_bound (primes.begin(), primes.end(), prime_window_base + WINDOW_SIZE/2 * D, prime_data_cmp());
		    for ( ; it != primes.end(); ++it) {
			// Process just the middle row when WINDOW_REDUCTION_SIZE is one.
			if (WINDOW_REDUCTION_SIZE == 1 && next_prime != 0 && it->prime > prime_window_base + (WINDOW_SIZE/2 + 1) * D) break;
			// If prime is already paired and output, skip to next prime
			if (!it->is_valid) continue;
			// If already paired, skip to next prime
			if (it->is_paired()) continue;
			// Relocatables are not paired until last_prime_window is set
			if (!it->must_pair_or_single && !it->last_prime_window) continue;
			// Relocatables where the base prime is already tentatively paired must be skipped
			if (!it->must_pair_or_single && used_relocs.find (it->base_prime) != used_relocs.end()) continue;

			// Create a queue of link endpoints to examine.  If the endpoint could instead point to a currently unpaired prime,
			// then we can rearrange the links so that both unpaired primes are paired.
			std::queue<prime_data*> unexamined;

			// Start the queue off with the original unpaired prime
			unexamined.push (&*it);

			int	best_rank;
			prime_data *p_best_pairing, *p_best_chain_end;

			// Examine the unexamined queue hoping one entry will provide a link to an unpaired prime that let's us create a new pairing.
			p_best_pairing = NULL;
			p_best_chain_end = NULL;
			next_backlink_id ();
			for (int num_examined = 0; !unexamined.empty(); num_examined++) {
//GW fine tune this
				if (WINDOWED && MAX_LINKS && num_examined > MAX_LINKS) break;

				// Pop the unexamined link endpoint from the queue
				auto p_unexamined = unexamined.front();
				unexamined.pop ();

				// When using prime windows and this is not the last prime window, we want to maximize the pairings in the lower half
				// of the window because entries in the upper half of the window will have a chance to pair with primes to be read in later.
				if (WINDOWED && next_prime != 0 && it->must_pair_or_single)
				// If unexamined is not required to pair during this prime window or is a prime in the upper half of the prime window,
				// then we can rearrange the links to guarantee the original entry in the lower half of the prime window gets paired.
				// This will not increase the number of pairs.  If we unpair a prime in the second half of the window, we'll later look
				// for a different way to pair that prime.  Example: Starting point A can-pair-to B and B paired-to reloc-or-big-prime-C.
				// Change this to A paired-to B, free reloc-or-big-prime-C.
				if ((!p_unexamined->must_pair_or_single && !p_unexamined->last_prime_window) || p_unexamined->prime > it->prime) {
					prime_data *p_link_start, *p_link_end;
					p_link_end = p_unexamined->is_paired() ? p_unexamined : p_unexamined->get_backlink ();
					ASSERTG (p_link_end != NULL);
					p_link_start = p_link_end->p_paired_with;
					// Break the link to this relocatable
					p_link_end->p_paired_with = NULL;
					p_link_start->p_paired_with = NULL;
					tot_pairs--;
					// Maintain the used relocations
					if (!p_link_end->must_pair_or_single) used_relocs.erase (p_link_end->base_prime);
					if (!p_link_start->must_pair_or_single) used_relocs.erase (p_link_start->base_prime);
					// Set up for rearranging the remaining links, then break out of loop to rearrange links
					p_best_pairing = p_link_start->must_pair_or_single ? p_link_start : p_link_start->get_backlink ();
					p_best_chain_end = p_link_end->get_backlink ();
					break;
				}

				// Look at each possible prime reachable from the unexamined link endpoint using
				// both relative prime and alternate relative prime distances
#ifndef ON_THE_FLY_CAN_PAIR_TO
				// Examine each possible prime that unexamined can pair to
				for (uint8_t i = 0; i < p_unexamined->num_can_pair_to; i++) {
					auto p_next_in_chain = can_pair_to[p_unexamined->first_can_pair_to + i];
#else
				uint32_t can_pair_to[100];
				int num_can_pair_to = 0;
				for (int link_type : {0,1}) {
					distances *d;
					if (link_type == 0) {
						if (!p_unexamined->relp) continue;
						d = &relp_distances[p_unexamined->relp];
					} else {
						if (!p_unexamined->alt_relp) continue;
						d = &alt_relp_distances[p_unexamined->alt_relp];
					}
					// Examine each possible distance
					for (auto && dist : *d) {
						uint64_t next_in_chain = p_unexamined->prime + dist;
						// Ignore possible nexts above the end of the prime window
						if (WINDOWED && next_in_chain > prime_window_base + WINDOW_SIZE * D) continue;
						// Make sure the next in chain is generated using a D value that is between B2_start and B2_end
						if ((p_unexamined->prime + next_in_chain) / 2 < B2_start ||
						    (p_unexamined->prime + next_in_chain) / 2 >= B2_end) continue;
						// We can't safely use the fast prime_map for primes below prime_window_base.
						if (next_in_chain < prime_window_base) {
							if (prime_window_base != B2_start) continue;
							auto itfind = std::lower_bound (primes.begin(), primes.end(), next_in_chain, prime_data_cmp());
							if (itfind->prime != next_in_chain) continue;
							ASSERTG (itfind->prime == next_in_chain);
							can_pair_to[num_can_pair_to++] = (uint32_t) (itfind - primes.begin());
						}
						// Finally, make sure the next in chain is part of this primes window using the fast prime_map
						else {
							auto primes_index = primes_map.find (next_in_chain);
							if (primes_index == primes_map.NOT_IN_PRIME_MAP) continue;
							ASSERTG (primes[primes_index].prime == next_in_chain);
							can_pair_to[num_can_pair_to++] = primes_index;
						}
					}
				}
				// Examine each possible prime that unexamined can pair to
				for (int i = 0; i < num_can_pair_to; i++) {
					auto p_next_in_chain = &primes[can_pair_to[i]];
#endif

					int	new_best_rank;

					// Catch easy case where next in chain has been seen before, then skip this possible next in chain
					if (p_unexamined->is_paired() && p_next_in_chain == p_unexamined->p_paired_with) continue;

					// If next_in_chain has a backlink, then we've visited the node already
					if (p_next_in_chain->has_backlink (backlink_id)) continue;

					// Special handling for relocatable primes having multiple relocations within the prime window
					if (!p_next_in_chain->must_pair_or_single) {
						// Make sure the original unpaired and next in chain are not using the same base prime
						if (it->base_prime == p_next_in_chain->base_prime) continue;
						// If the relocatable is tentatively paired we must follow that link instead.
						// The back pointer will tell us if we've already visited this base prime.
						auto it_used = used_relocs.find (p_next_in_chain->base_prime);
						if (it_used != used_relocs.end()) {
							auto p_paired_reloc = it_used->second;
							if (p_paired_reloc->has_backlink (backlink_id)) continue;
							p_paired_reloc->set_backlink (backlink_id, p_next_in_chain);
							p_next_in_chain = p_paired_reloc;
						}
					}

					// If next in chain is a new link we might need to follow it.  Skip it if either it or a relocation
					// of it has previously been seen.  Otherwise, add it to the queue of unexamined connections.
					if (p_next_in_chain->is_paired()) {
						auto p_pair = p_next_in_chain->p_paired_with;
						if (p_pair->has_backlink (backlink_id)) continue;
						p_pair->set_backlink (backlink_id, p_unexamined);
						unexamined.push (p_pair);
						// If the link is to a relocatable prime that is required to pair during this prime window, then we
						// can pair with any relocation of the base prime in the prime window.  Add those relocations to the queue.
						// Example: A can-pair-to B and B paired-to reloc-of-C and other-reloc-of-C can-pair-to unpaired D.
						// Change to A paired-to B and other-reloc-of-C paired-to D.
						if (p_pair->must_pair_or_single) continue;
						auto range = relocs.equal_range (p_pair->base_prime);
						for (auto it2 = range.first; it2 != range.second; ++it2) {
							if (it2->second == p_pair) continue;
							it2->second->set_backlink (backlink_id, p_pair);
							unexamined.push (it2->second);
						}
						continue;
					}

					// If we've found the original unpaired prime, that is not a success
					if (p_next_in_chain->prime == it->prime) continue;

					// We've found an unpaired prime!  Calculate its "goodness".
					// The ideal ending is a next-in-chain that is must-pair.
					// Next best is a relocatable that must pair this prime window.
					// Worst case is next-in-chain is relocatable that can be used in future prime windows.
					new_best_rank = p_next_in_chain->score_reloc ();

					// If this solution is better than any previously found, remember it.
					// When choosing between two relocatables use the one with the larger base prime as that will
					// will have more (or equal) possible future relocations.
					if (p_best_pairing == NULL ||
					    best_rank < new_best_rank ||
					    (best_rank == new_best_rank && p_next_in_chain->base_prime > p_best_pairing->base_prime)) {
						best_rank = new_best_rank;
						p_best_pairing = p_next_in_chain;
						p_best_chain_end = p_unexamined;
					}
				}
				// For non-windowed version, break when first possible pairing is found
				if ((!WINDOWED || next_prime == 0) && p_best_pairing != NULL) break;
//GW: we could be even pickier (and slower) -- demanding a must-pair-or-single in the lower half
if (!FIRST_EXIT && WINDOWED && next_prime != 0 && p_best_pairing != NULL && p_best_pairing->must_pair_or_single) break;
if (FIRST_EXIT && WINDOWED && next_prime != 0 && p_best_pairing != NULL) break;
			}

			// If we found no unpaired primes to end our chain, examine the next unpaired prime
			if (p_best_pairing == NULL) continue;
			ASSERTG (!p_best_pairing->is_paired());
			ASSERTG (p_best_pairing->must_pair_or_single || used_relocs.find (p_best_pairing->base_prime) == used_relocs.end());
			ASSERTG (it->must_pair_or_single || used_relocs.find (it->base_prime) == used_relocs.end());

			// Unravel the links so that both the original unpaired prime and best_pairing are now both paired!
			// In this loop, the following variables are used (and what they mean).
			//   best_pairing -- end point of the link being created
			//   best_chain_end -- start point of the link being created (equals original unpaired prime on last link creation)
			//   link_end -- end point of the link being destroyed (often this is the same as best_chain_end)
			//   link_start -- start point of the link being destroyed (zero if there is no link to destroy -- that is loop is complete)
			for ( ; ; ) {
				prime_data *p_link_start, *p_link_end;

				// Get the end point of the link being destroyed (usually the same as best_chain_end)
				if (it->prime == p_best_chain_end->prime || p_best_chain_end->is_paired()) p_link_end = p_best_chain_end;
				else p_link_end = p_best_chain_end->get_backlink ();
				ASSERTG (p_link_end != NULL);

				// Get the start point of the link being destroyed (will be NULL when creating last link)
				if (p_link_end->is_paired()) p_link_start = p_link_end->p_paired_with;
				else p_link_start = NULL;

				// Break the old link
				if (p_link_start != NULL) {
					ASSERTG (p_link_start->is_paired() && p_link_end->is_paired());
					p_link_start->p_paired_with = NULL;
					p_link_end->p_paired_with = NULL;
					if (!p_link_start->must_pair_or_single) used_relocs.erase (p_link_start->base_prime);
					if (!p_link_end->must_pair_or_single) used_relocs.erase (p_link_end->base_prime);
				}

				// Create the new link except when doing prime windows and this would be a reloc-to-reloc pairing -- we can save the
				// two relocs for possible use at a later date!  In that case, simply decrement the pairs count.
				if (WINDOWED && next_prime != 0 &&
				    !p_best_pairing->must_pair_or_single && !p_best_pairing->last_prime_window &&
				    !p_best_chain_end->must_pair_or_single && !p_best_chain_end->last_prime_window) {
					tot_pairs--;
				} else {
					pgen.validate_pairing (p_best_chain_end->prime, p_best_pairing->prime, D, ALT_D, ALT_D_OFFSET, B2_start, B2_end, relps);
					ASSERTG (!p_best_pairing->is_paired() && !p_best_chain_end->is_paired());
					p_best_pairing->p_paired_with = p_best_chain_end;
					p_best_chain_end->p_paired_with = p_best_pairing;
					// Keep track of used relocs
					ASSERTG (p_best_pairing->must_pair_or_single || used_relocs.find (p_best_pairing->base_prime) == used_relocs.end());
					ASSERTG (p_best_chain_end->must_pair_or_single || used_relocs.find (p_best_chain_end->base_prime) == used_relocs.end());
					if (!p_best_pairing->must_pair_or_single) used_relocs[p_best_pairing->base_prime] = p_best_pairing;
					if (!p_best_chain_end->must_pair_or_single) used_relocs[p_best_chain_end->base_prime] = p_best_chain_end;
				}

				// If we've modified all the links in the chain, break out of the loop
				if (p_link_start == NULL) {
					tot_pairs++;
					break;
				}

				// Move to previous link in the chain
				p_best_pairing = p_link_start->must_pair_or_single ? p_link_start : p_link_start->get_backlink ();
				p_best_chain_end = p_link_end->get_backlink ();
			}
		    }
		}

/* Output the lower part of the primes window so that we have room to add new primes to the window */

		while (!primes.empty()) {
			auto it = primes.begin();

			// If prime is already paired and output, pop it
			if (!it->is_valid) { primes_map.pop_front(it->prime); primes.pop_front(); continue; }

			// Break when we reach first prime that is above the window reduction threshold
			if (next_prime != 0 && it->prime > prime_window_base + WINDOW_REDUCTION_SIZE * D) break;

			// If prime is paired, output the pair
			if (it->is_paired()) {
				// Validate the pairing
				auto p_pair = it->p_paired_with;
				pgen.validate_prime (it->prime, it->base_prime);
				pgen.validate_prime (p_pair->prime, p_pair->base_prime);
				pgen.validate_pairing (it->prime, p_pair->prime, D, ALT_D, ALT_D_OFFSET, B2_start, B2_end, relps);
				// no reloc-to-reloc pairs allowed when windowing
				ASSERTG (!WINDOWED || next_prime == 0 || it->must_pair_or_single || it->last_prime_window || p_pair->must_pair_or_single || p_pair->last_prime_window);
				output_pair (it->prime, p_pair->prime, D, ALT_D, ALT_D_OFFSET, B2_start, totrels, relps, pairmap_data);
				// If relocatable, remove from prime generator, primes window, and the relocs maps
				if (it->base_prime) {
					pgen.remove_recycled (it->prime, it->base_prime);
					// If we're pairing from a multi-reloc erase other instances of the base prime
					auto range = relocs.equal_range (it->base_prime);
					for (auto it2 = range.first; it2 != range.second; ++it2) if (it2->second != &*it) {
						primes_map.erase (it2->second->prime);
						it2->second->is_valid = FALSE;
					}
					relocs.erase (it->base_prime);
					used_relocs.erase (it->base_prime);
				}
				if (p_pair->base_prime) {
					pgen.remove_recycled (p_pair->prime, p_pair->base_prime);
					// If we're pairing from a multi-reloc erase other instances of the base prime
					auto range = relocs.equal_range (p_pair->base_prime);
					for (auto it2 = range.first; it2 != range.second; ++it2) if (it2->second != p_pair) {
						primes_map.erase (it2->second->prime);
						it2->second->is_valid = FALSE;
					}
					relocs.erase (p_pair->base_prime);
					used_relocs.erase (p_pair->base_prime);
				}
				// Remove the pair from the primes window
				primes_map.erase (p_pair->prime);
				p_pair->is_valid = FALSE;
				primes_map.pop_front (it->prime);
				primes.pop_front ();
				continue;
			}

			// Special handling for unpaired relocatable primes
			if (it->base_prime) {
				// Unless this is the last occurence of this base prime, simply erase this instance
				if (!it->last_prime_window || relocs.count (it->base_prime) > 1) {
					auto range = relocs.equal_range (it->base_prime);
					for (auto it2 = range.first; it2 != range.second; ++it2)
						if (it2->second == &*it) { relocs.erase (it2); break; }
					primes_map.pop_front (it->prime);
					primes.pop_front ();
					continue;
				}

				// Aack, a single.  Erase from the relocs map, fall through to single-handling code.
				ASSERTG (used_relocs.find (it->base_prime) == used_relocs.end());
				relocs.erase (it->base_prime);
				reloc_singles++;
			}

			// Ugh, a single
			// Validate the single
			pgen.validate_prime (it->prime, it->base_prime);
			// Generate a fake pairing being extra careful to find a valid pairing when the single is past B2_end.
			int pairing_set, prime_relp;
			uint64_t pairing_Dblock;
			for (i = 0; i < int_multiplier; i++) {
				pairing_set = relp_sets[i];
				prime_relp = it->relp;
				// Negative pairing sets need to adjust both prime_relp and pairing_set
				if (pairing_set < 0) {
					// it->relp is always between -D/2 and D/2.  Negative sets can only access the +/-(D/2 to D) relps.
					if (prime_relp < 0) prime_relp += D; else prime_relp -= D;
					pairing_set = -1-pairing_set;
				}
				// Negative and positive prime_relps find their pairing Dblock in different directions
				if (prime_relp >= 0) pairing_Dblock = it->prime - prime_relp - pairing_set*D;
				else pairing_Dblock = it->prime - prime_relp + pairing_set*D;
				// Validate that this D block is valid
				if (pairing_Dblock > B2_start && pairing_Dblock < B2_end) break;
			}
			// Calculate the pairing prime
			uint64_t pairing_prime;
			if (prime_relp >= 0) pairing_prime = pairing_Dblock - prime_relp - pairing_set*D;
			else pairing_prime = pairing_Dblock - prime_relp + pairing_set*D;
			// Validate and output the pairing
			pgen.validate_pairing (it->prime, pairing_prime, D, ALT_D, ALT_D_OFFSET, B2_start, B2_end, relps);
			output_pair (it->prime, pairing_prime, D, ALT_D, ALT_D_OFFSET, B2_start, totrels, relps, pairmap_data);
			tot_singles++;
			// Remove single from primes window
			primes_map.pop_front (it->prime);
			primes.pop_front ();
		}

		// Bump the minimum prime stored in the primes window
		prime_window_base += WINDOW_REDUCTION_SIZE * D;

/* Output some of the pairmap.  Once we are certain no pairs/singles will be output below x, we can output differences for all pairs/singles below x. */

		for ( ; ; ) {
			auto it = pairmap_data.begin();
			if (it == pairmap_data.end()) break;
			// If we might output a smaller pair at a later date, then stop outputting differences.
			// This can happen if there are more primes to process and there are primes in the prime window that can be "reached" by max_relp.
			if (next_prime && divide_rounding_up (*it, totrels) * D + B2_start + max_relp > prime_window_base) break;
			// Grow the pairmap if necessary
			size_t current_len = pairmap_ptr - *pairmap;
			if ((uint64_t) (current_len + 3) > *pairmap_size) {
				*pairmap_size = (uint64_t) ((double) *pairmap_size * 1.2);
				*pairmap = (uint8_t *) realloc (*pairmap, (size_t) *pairmap_size);
				if (*pairmap == NULL) goto oom;
				pairmap_ptr = *pairmap + current_len;
			}
			// Output the difference as a one or two byte value
			int32_t difference = (int32_t) *it - last_pair_output;
			ASSERTG (difference > 0);
			if (difference <= 255) {
				*pairmap_ptr++ = (uint8_t) difference;
			} else {
				ASSERTG (difference <= 65535);
				*pairmap_ptr++ = (uint8_t) 0;			// Special code to indicate two-byte difference
				*pairmap_ptr++ = (uint8_t) (difference >> 8);
				*pairmap_ptr++ = (uint8_t) difference;
			}
			// Remember pair just output so we can calculate next difference
			last_pair_output = (int32_t) *it;
			// Erase the just output pair data
			pairmap_data.erase (it);
		}

		// Break when no more primes to process
		if (next_prime == 0) break;
	}

	// Perform a few final checks.  But first adjust for relocatables that we hoped to pair but did not.
	pgen.validate_adjust (relps);
	ASSERTG (pgen.get_numprimes () == tot_pairs * 2 + tot_singles);
	pgen.validate_final ();

/* Output an informational message on our pairing efficiency */

	{
		char buf[120];
		sprintf (buf, "D: %d, relative primes: %d, stage 2 primes: %" PRIu64 ", pair%%=%5.2f\n",
			 D, totrels, pgen.get_numprimes (), (double) tot_pairs * 2.0 / (double) pgen.get_numprimes () * 100.0);
		OutputStr (thread_num, buf);
	}

/* Shrink the pairmap to its minimum size */

	*pairmap_size = (uint64_t) (pairmap_ptr - *pairmap);
	*pairmap = (uint8_t *) realloc (*pairmap, (size_t) *pairmap_size);
	if (*pairmap == NULL) goto oom;

/* All done */

	return (0);

/* Out of memory exit */

oom:	return (OutOfMemory (thread_num));
}

/**********************************************************************************************************************/
/*                                                Pair map access                                                     */
/**********************************************************************************************************************/

/* Get distance to next pair in the pair map */

uint32_t next_pair (			/* Returns distance to next prime pairing (or single) */
	uint8_t	**pairmap_ptr)		/* Current pointer into the pairing map generated by fill_pairmap -- this will be modified */
{
	uint8_t *p = *pairmap_ptr;
	uint32_t diff;			/* Distance to next pairing */

	if (*p) {
		diff = *p++;		/* One byte difference */
	} else {
		p++;			/* Skip the special zero escape byte */
		diff = *p++ << 8;	/* Get high half of difference */
		diff += *p++;		/* Add low half of difference */
	}

/* Return difference and updated pointer */

	*pairmap_ptr = p;
	return (diff);
}
