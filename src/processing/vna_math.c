/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Originally written using elements from Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "nanovna.h"
#include "processing/dsp_config.h"
#include <stdint.h>

// Use table increase transform speed, but increase code size
// Use compact table, need 1/4 code size, and not decrease speed
// Used only if not defined __VNA_USE_MATH_TABLES__ (use self table for TTF or direct sin/cos
// calculations)
#define FFT_USE_SIN_COS_TABLE

#ifdef NANOVNA_F303
// =================================================================================
// F303 IMPLEMENTATION
// =================================================================================

#define QTR_WAVE_TABLE_SIZE 257

static const float sin_table_qtr[QTR_WAVE_TABLE_SIZE] = {
  0.000000000000000f, 0.006135884649154f, 0.012271538285720f, 0.018406729905805f,
  0.024541228522912f, 0.030674803176637f, 0.036807222941359f, 0.042938256934941f,
  0.049067674327418f, 0.055195244349690f, 0.061320736302209f, 0.067443919563664f,
  0.073564563599667f, 0.079682437971430f, 0.085797312344440f, 0.091908956497133f,
  0.098017140329561f, 0.104121633872055f, 0.110222207293883f, 0.116318630911905f,
  0.122410675199216f, 0.128498110793793f, 0.134580708507126f, 0.140658239332849f,
  0.146730474455362f, 0.152797185258443f, 0.158858143333861f, 0.164913120489970f,
  0.170961888760301f, 0.177004220412149f, 0.183039887955141f, 0.189068664149806f,
  0.195090322016128f, 0.201104634842092f, 0.207111376192219f, 0.213110319916091f,
  0.219101240156870f, 0.225083911359793f, 0.231058108280671f, 0.237023605994367f,
  0.242980179903264f, 0.248927605745720f, 0.254865659604515f, 0.260794117915276f,
  0.266712757474898f, 0.272621355449949f, 0.278519689385053f, 0.284407537211272f,
  0.290284677254462f, 0.296150888243624f, 0.302005949319228f, 0.307849640041535f,
  0.313681740398892f, 0.319502030816016f, 0.325310292162263f, 0.331106305759876f,
  0.336889853392220f, 0.342660717311994f, 0.348418680249435f, 0.354163525420490f,
  0.359895036534988f, 0.365612997804774f, 0.371317193951838f, 0.377007410216418f,
  0.382683432365090f, 0.388345046698826f, 0.393992040061048f, 0.399624199845647f,
  0.405241314004990f, 0.410843171057904f, 0.416429560097637f, 0.422000270799800f,
  0.427555093430282f, 0.433093818853152f, 0.438616238538528f, 0.444122144570429f,
  0.449611329654607f, 0.455083587126344f, 0.460538710958240f, 0.465976495767966f,
  0.471396736825998f, 0.476799230063322f, 0.482183772079123f, 0.487550160148436f,
  0.492898192229784f, 0.498227666972782f, 0.503538383725718f, 0.508830142543107f,
  0.514102744193222f, 0.519355990165590f, 0.524589682678469f, 0.529803624686295f,
  0.534997619887097f, 0.540171472729893f, 0.545324988422046f, 0.550457972936605f,
  0.555570233019602f, 0.560661576197336f, 0.565731810783613f, 0.570780745886967f,
  0.575808191417845f, 0.580813958095765f, 0.585797857456439f, 0.590759701858874f,
  0.595699304492433f, 0.600616479383869f, 0.605511041404326f, 0.610382806276309f,
  0.615231590580627f, 0.620057211763289f, 0.624859488142386f, 0.629638238914927f,
  0.634393284163645f, 0.639124444863776f, 0.643831542889791f, 0.648514401022112f,
  0.653172842953777f, 0.657806693297079f, 0.662415777590172f, 0.666999922303637f,
  0.671558954847018f, 0.676092703575316f, 0.680600997795453f, 0.685083667772700f,
  0.689540544737067f, 0.693971460889654f, 0.698376249408973f, 0.702754744457225f,
  0.707106781186547f, 0.711432195745216f, 0.715730825283819f, 0.720002507961382f,
  0.724247082951467f, 0.728464390448225f, 0.732654271672413f, 0.736816568877370f,
  0.740951125354959f, 0.745057785441466f, 0.749136394523459f, 0.753186799043612f,
  0.757208846506484f, 0.761202385484262f, 0.765167265622459f, 0.769103337645580f,
  0.773010453362737f, 0.776888465673232f, 0.780737228572094f, 0.784556597155575f,
  0.788346427626606f, 0.792106577300212f, 0.795836904608883f, 0.799537269107905f,
  0.803207531480645f, 0.806847553543799f, 0.810457198252595f, 0.814036329705948f,
  0.817584813151584f, 0.821102514991105f, 0.824589302785025f, 0.828045045257756f,
  0.831469612302545f, 0.834862874986380f, 0.838224705554838f, 0.841554977436898f,
  0.844853565249707f, 0.848120344803297f, 0.851355193105265f, 0.854557988365401f,
  0.857728610000272f, 0.860866938637767f, 0.863972856121587f, 0.867046245515693f,
  0.870086991108711f, 0.873094978418290f, 0.876070094195407f, 0.879012226428633f,
  0.881921264348355f, 0.884797098430938f, 0.887639620402854f, 0.890448723244758f,
  0.893224301195515f, 0.895966249756185f, 0.898674465693954f, 0.901348847046022f,
  0.903989293123443f, 0.906595704514915f, 0.909167983090522f, 0.911706032005430f,
  0.914209755703531f, 0.916679059921043f, 0.919113851690058f, 0.921514039342042f,
  0.923879532511287f, 0.926210242138311f, 0.928506080473215f, 0.930766961078984f,
  0.932992798834739f, 0.935183509938947f, 0.937339011912575f, 0.939459223602190f,
  0.941544065183021f, 0.943593458161960f, 0.945607325380521f, 0.947585591017741f,
  0.949528180593037f, 0.951435020969008f, 0.953306040354194f, 0.955141168305771f,
  0.956940335732209f, 0.958703474895872f, 0.960430519415566f, 0.962121404269042f,
  0.963776065795440f, 0.965394441697689f, 0.966976471044852f, 0.968522094274417f,
  0.970031253194544f, 0.971503890986252f, 0.972939952205560f, 0.974339382785576f,
  0.975702130038529f, 0.977028142657754f, 0.978317370719628f, 0.979569765685441f,
  0.980785280403230f, 0.981963869109555f, 0.983105487431216f, 0.984210092386929f,
  0.985277642388941f, 0.986308097244599f, 0.987301418157858f, 0.988257567730749f,
  0.989176509964781f, 0.990058210262297f, 0.990902635427780f, 0.991709753669100f,
  0.992479534598710f, 0.993211949234795f, 0.993906970002356f, 0.994564570734255f,
  0.995184726672197f, 0.995767414467660f, 0.996312612182778f, 0.996820299291166f,
  0.997290456678690f, 0.997723066644192f, 0.998118112900149f, 0.998475580573295f,
  0.998795456205172f, 0.999077727752645f, 0.999322384588350f, 0.999529417501093f,
  0.999698818696204f, 0.999830581795823f, 0.999924701839145f, 0.999981175282601f,
  1.000000000000000f
};

// Forward declaration
float vna_modff(float x, float* iptr);

// Interpolation helper for 257-point table (0..256)
// QTR_WAVE_TABLE_SIZE is 257

static inline float quadratic_interpolation_qtr(float x) {
    int idx = (int)x;
    float fract = x - idx;
    if (idx < 0) {
        idx = 0;
        fract = 0.0f; 
    }
    // Table has QTR_WAVE_TABLE_SIZE entries (0..256)
    if (idx >= QTR_WAVE_TABLE_SIZE - 1) {
        return sin_table_qtr[QTR_WAVE_TABLE_SIZE - 1];
    }
    float y0, y1, y2;
    if (idx >= QTR_WAVE_TABLE_SIZE - 2) { 
        y0 = sin_table_qtr[idx];
        y1 = sin_table_qtr[idx + 1];
        y2 = y0; // Mirror around pi/2; do not flatten the final interval.
    } else {
        y0 = sin_table_qtr[idx];
        y1 = sin_table_qtr[idx + 1];
        y2 = sin_table_qtr[idx + 2];
    }
    
    // Standard quadratic interpolation
    return y0 + fract * (y1 - y0) + fract * (fract - 1.0f) * (y2 - 2.0f * y1 + y0) * 0.5f;
}

// Corrected vna_sincosf specific for F303 high-res table (1024 full circle, 257-point table)
void vna_sincosf(float angle, float * pSinVal, float * pCosVal) {
  float fpart, ipart;
  fpart = vna_modff(angle, &ipart);  
  if (fpart < 0.0f) fpart += 1.0f; 
  
  // Full circle is 1024 steps (256 * 4)
  const float table_size_full_circle = 1024.0f;
  const uint16_t table_quarter = 256; // Steps in quarter (LAST INDEX)
  
  float scaled = fpart * table_size_full_circle;
  uint16_t full_index = (uint16_t)scaled;
  float fract = scaled - full_index;
  
  // Quadrant 0..3
  uint8_t quad = full_index / table_quarter;
  // Position in quadrant 0..255
  uint16_t in_quad_pos = full_index % table_quarter;
  
  float sin_interp = quadratic_interpolation_qtr(in_quad_pos + fract);
  
  // Cosine is complementary
  float comp_angle = (float)table_quarter - (in_quad_pos + fract);
  float cos_interp = quadratic_interpolation_qtr(comp_angle);

  float sin_final, cos_final;
  switch (quad) {
    case 0: sin_final = sin_interp; cos_final = cos_interp; break;
    case 1: sin_final = cos_interp; cos_final = -sin_interp; break;
    case 2: sin_final = -sin_interp; cos_final = -cos_interp; break;
    case 3: sin_final = -cos_interp; cos_final = sin_interp; break;
    default: sin_final = 0.0f; cos_final = 1.0f; break;
  }
  *pSinVal = sin_final; *pCosVal = cos_final;
}

// FFT macros for F303
// Adaptive scaling based on FFT_SIZE
#define FFT_Q (FFT_SIZE/4)
#define FFT_H (FFT_SIZE/2)
// Every FFT twiddle is retained exactly from the original 513-entry table.
#define TABLE_QUARTER_STEPS (QTR_WAVE_TABLE_SIZE - 1) // 256
#define FFT_TABLE_SCALE (TABLE_QUARTER_STEPS / FFT_Q)
_Static_assert(TABLE_QUARTER_STEPS >= FFT_Q && TABLE_QUARTER_STEPS % FFT_Q == 0,
               "sine table must contain every FFT twiddle");

#define FFT_SIN(i) ((i) > FFT_Q ? sin_table_qtr[(FFT_H-(i))*FFT_TABLE_SCALE] : sin_table_qtr[(i)*FFT_TABLE_SCALE])
#define FFT_COS(i) ((i) > FFT_Q ?-sin_table_qtr[((i)-FFT_Q)*FFT_TABLE_SCALE] : sin_table_qtr[(FFT_Q-(i))*FFT_TABLE_SCALE])

// Reference modff
float vna_modff(float x, float *iptr)
{
  union {float f; uint32_t i;} u = {x};
  int e = (int)((u.i>>23)&0xff) - 0x7f; // get exponent
  if (e <   0) {                        // no integral part
    if (iptr) *iptr = 0;
    return u.f;
  }
  if (e >= 23) x = 0;                   // no fractional part
  else {
    x = u.f; u.i&= ~(0x007fffff>>e);    // remove fractional part from u
    x-= u.f;                            // calc fractional part
  }
  if (iptr) *iptr = u.f;                // cut integer part from float as float
  return x;
}
#else
// =================================================================================
// F072 IMPLEMENTATION
// =================================================================================

#define QTR_WAVE_TABLE_SIZE 257


// F072 Table (0 to 90 degrees, QTR_WAVE_TABLE_SIZE values)
static const float sin_table_qtr[QTR_WAVE_TABLE_SIZE] = {
  0.000000000000000f,   0.006135884649154f,   0.012271538285720f,   0.018406729905805f,
  0.024541228522912f,   0.030674803176637f,   0.036807222941359f,   0.042938256934941f,
  0.049067674327418f,   0.055195244349690f,   0.061320736302209f,   0.067443919563664f,
  0.073564563599667f,   0.079682437971430f,   0.085797312344440f,   0.091908956497133f,
  0.098017140329561f,   0.104121633872055f,   0.110222207293883f,   0.116318630911905f,
  0.122410675199216f,   0.128498110793793f,   0.134580708507126f,   0.140658239332849f,
  0.146730474455362f,   0.152797185258443f,   0.158858143333861f,   0.164913120489970f,
  0.170961888760301f,   0.177004220412149f,   0.183039887955141f,   0.189068664149806f,
  0.195090322016128f,   0.201104634842092f,   0.207111376192219f,   0.213110319916091f,
  0.219101240156870f,   0.225083911359793f,   0.231058108280671f,   0.237023605994367f,
  0.242980179903264f,   0.248927605745720f,   0.254865659604515f,   0.260794117915276f,
  0.266712757474898f,   0.272621355449949f,   0.278519689385053f,   0.284407537211272f,
  0.290284677254462f,   0.296150888243624f,   0.302005949319228f,   0.307849640041535f,
  0.313681740398892f,   0.319502030816016f,   0.325310292162263f,   0.331106305759876f,
  0.336889853392220f,   0.342660717311994f,   0.348418680249435f,   0.354163525420490f,
  0.359895036534988f,   0.365612997804774f,   0.371317193951838f,   0.377007410216418f,
  0.382683432365090f,   0.388345046698826f,   0.393992040061048f,   0.399624199845647f,
  0.405241314004990f,   0.410843171057904f,   0.416429560097637f,   0.422000270799800f,
  0.427555093430282f,   0.433093818853152f,   0.438616238538528f,   0.444122144570429f,
  0.449611329654607f,   0.455083587126344f,   0.460538710958240f,   0.465976495767966f,
  0.471396736825998f,   0.476799230063322f,   0.482183772079123f,   0.487550160148436f,
  0.492898192229784f,   0.498227666972782f,   0.503538383725718f,   0.508830142543107f,
  0.514102744193222f,   0.519355990165590f,   0.524589682678469f,   0.529803624686295f,
  0.534997619887097f,   0.540171472729893f,   0.545324988422046f,   0.550457972936605f,
  0.555570233019602f,   0.560661576197336f,   0.565731810783613f,   0.570780745886967f,
  0.575808191417845f,   0.580813958095765f,   0.585797857456439f,   0.590759701858874f,
  0.595699304492433f,   0.600616479383869f,   0.605511041404326f,   0.610382806276309f,
  0.615231590580627f,   0.620057211763289f,   0.624859488142386f,   0.629638238914927f,
  0.634393284163645f,   0.639124444863776f,   0.643831542889791f,   0.648514401022112f,
  0.653172842953777f,   0.657806693297079f,   0.662415777590172f,   0.666999922303637f,
  0.671558954847018f,   0.676092703575316f,   0.680600997795453f,   0.685083667772700f,
  0.689540544737067f,   0.693971460889654f,   0.698376249408973f,   0.702754744457225f,
  0.707106781186547f,   0.711432195745216f,   0.715730825283819f,   0.720002507961382f,
  0.724247082951467f,   0.728464390448225f,   0.732654271672413f,   0.736816568877370f,
  0.740951125354959f,   0.745057785441466f,   0.749136394523459f,   0.753186799043612f,
  0.757208846506484f,   0.761202385484262f,   0.765167265622459f,   0.769103337645580f,
  0.773010453362737f,   0.776888465673232f,   0.780737228572094f,   0.784556597155575f,
  0.788346427626606f,   0.792106577300212f,   0.795836904608883f,   0.799537269107905f,
  0.803207531480645f,   0.806847553543799f,   0.810457198252595f,   0.814036329705948f,
  0.817584813151584f,   0.821102514991105f,   0.824589302785025f,   0.828045045257756f,
  0.831469612302545f,   0.834862874986380f,   0.838224705554838f,   0.841554977436898f,
  0.844853565249707f,   0.848120344803297f,   0.851355193105265f,   0.854557988365401f,
  0.857728610000272f,   0.860866938637767f,   0.863972856121587f,   0.867046245515693f,
  0.870086991108711f,   0.873094978418290f,   0.876070094195407f,   0.879012226428633f,
  0.881921264348355f,   0.884797098430938f,   0.887639620402854f,   0.890448723244758f,
  0.893224301195515f,   0.895966249756185f,   0.898674465693954f,   0.901348847046022f,
  0.903989293123443f,   0.906595704514915f,   0.909167983090522f,   0.911706032005430f,
  0.914209755703531f,   0.916679059921043f,   0.919113851690058f,   0.921514039342042f,
  0.923879532511287f,   0.926210242138311f,   0.928506080473215f,   0.930766961078984f,
  0.932992798834739f,   0.935183509938947f,   0.937339011912575f,   0.939459223602190f,
  0.941544065183021f,   0.943593458161960f,   0.945607325380521f,   0.947585591017741f,
  0.949528180593037f,   0.951435020969008f,   0.953306040354194f,   0.955141168305771f,
  0.956940335732209f,   0.958703474895872f,   0.960430519415566f,   0.962121404269042f,
  0.963776065795440f,   0.965394441697689f,   0.966976471044852f,   0.968522094274417f,
  0.970031253194544f,   0.971503890986252f,   0.972939952205560f,   0.974339382785576f,
  0.975702130038529f,   0.977028142657754f,   0.978317370719628f,   0.979569765685441f,
  0.980785280403230f,   0.981963869109555f,   0.983105487431216f,   0.984210092386929f,
  0.985277642388941f,   0.986308097244599f,   0.987301418157858f,   0.988257567730749f,
  0.989176509964781f,   0.990058210262297f,   0.990902635427780f,   0.991709753669100f,
  0.992479534598710f,   0.993211949234795f,   0.993906970002356f,   0.994564570734255f,
  0.995184726672197f,   0.995767414467660f,   0.996312612182778f,   0.996820299291166f,
  0.997290456678690f,   0.997723066644192f,   0.998118112900149f,   0.998475580573295f,
  0.998795456205172f,   0.999077727752645f,   0.999322384588350f,   0.999529417501093f,
  0.999698818696204f,   0.999830581795823f,   0.999924701839145f,   0.999981175282601f,
  1.000000000000000f 
};


// Use F072-specific table (from above)
static const float *sin_table_qtr_ptr = sin_table_qtr;

static inline float quadratic_interpolation(float x) {
    int idx = (int)x;
    float fract = x - idx;
    if (idx < 0) {
        idx = 0;
        fract = 0.0f; 
    }
    if (idx >= QTR_WAVE_TABLE_SIZE - 1) {
        return sin_table_qtr_ptr[QTR_WAVE_TABLE_SIZE - 1];
    }
    float y0, y1, y2;
    if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
        y0 = sin_table_qtr_ptr[idx];
        y1 = sin_table_qtr_ptr[idx + 1];
        y2 = y1;  
    } else {
        y0 = sin_table_qtr_ptr[idx];
        y1 = sin_table_qtr_ptr[idx + 1];
        y2 = sin_table_qtr_ptr[idx + 2];
    }
    if (idx >= QTR_WAVE_TABLE_SIZE - 2) {
        float t = (idx >= QTR_WAVE_TABLE_SIZE - 1) ? 1.0f : fract;
        return y0 + t * (y1 - y0);
    } else {
        return y0 + fract * (y1 - y0) + fract * (fract - 1.0f) * (y2 - 2.0f * y1 + y0) * 0.5f;
    }
}


// FFT macros for F072
// Adaptive scaling based on FFT_SIZE
// QTR_WAVE_TABLE_SIZE is 257 (indices 0..256)
#define FFT_Q (FFT_SIZE/4)
#define FFT_H (FFT_SIZE/2)
// Steps in quarter: 256
#define TABLE_QUARTER_STEPS (QTR_WAVE_TABLE_SIZE - 1) 
#define FFT_TABLE_SCALE (TABLE_QUARTER_STEPS / FFT_Q)

// Use direct table lookup for performance and code size on Cortex-M0
// Avoids software float multiplication/branching in inner loop
#define FFT_SIN(i) ((i) > FFT_Q ? sin_table_qtr[(FFT_H-(i))*FFT_TABLE_SCALE] : sin_table_qtr[(i)*FFT_TABLE_SCALE])
#define FFT_COS(i) ((i) > FFT_Q ?-sin_table_qtr[((i)-FFT_Q)*FFT_TABLE_SCALE] : sin_table_qtr[(FFT_Q-(i))*FFT_TABLE_SCALE])


// Original modff
float vna_modff(float x, float* iptr) {
  union {float f; uint32_t i;} u = {x};
  int e = (int)((u.i >> 23) & 0xff) - 0x7f; 
  if (e < 0) { if (iptr) *iptr = 0; return u.f; }
  if (e >= 23) x = 0; 
  else { x = u.f; u.i &= ~(0x007fffff >> e); x -= u.f; }
  if (iptr) *iptr = u.f; 
  return x;
}

// Original vna_sincosf
void vna_sincosf(float angle, float* pSinVal, float* pCosVal) {
#if !defined(__VNA_USE_MATH_TABLES__) || defined(NANOVNA_HOST_TEST)
  angle *= 2.0f * VNA_PI; *pSinVal = sinf(angle); *pCosVal = cosf(angle);
#else
  float fpart, ipart;
  fpart = vna_modff(angle, &ipart);  
  if (fpart < 0.0f) fpart += 1.0f; 
  const float table_size_per_quarter = 256.0f;   
  const float table_size_full_circle = 1024.0f;  
  float scaled = fpart * table_size_full_circle;
  uint16_t full_index = (uint16_t)scaled;
  float fract = scaled - full_index;
  uint8_t quad = full_index / (uint8_t)table_size_per_quarter;  
  uint16_t in_quad_pos = full_index % (uint16_t)table_size_per_quarter;  
  float sin_interp = quadratic_interpolation(in_quad_pos + fract);
  float comp_angle = table_size_per_quarter - (in_quad_pos + fract);
  float cos_interp;
  if (comp_angle >= table_size_per_quarter) {
      cos_interp = sin_table_qtr_ptr[QTR_WAVE_TABLE_SIZE - 1];
  } else if (comp_angle < 0.0f) {
      cos_interp = sin_table_qtr_ptr[0];
  } else {
      cos_interp = quadratic_interpolation(comp_angle);
  }
  float sin_final, cos_final;
  switch (quad) {
    case 0: sin_final = sin_interp; cos_final = cos_interp; break;
    case 1: sin_final = cos_interp; cos_final = -sin_interp; break;
    case 2: sin_final = -sin_interp; cos_final = -cos_interp; break;
    case 3: sin_final = -cos_interp; cos_final = sin_interp; break;
    default: sin_final = 0.0f; cos_final = 1.0f; break;
  }
  *pSinVal = sin_final; *pCosVal = cos_final;
#endif
}

// NOTE: Original vna_math.c did NOT contain vna_atan2f or vna_expf.
// Do not define them here.
// If the linker needs them, they likely come from headers or other files in the original setup.
// If not, adding them here might duplicate symbols in F072 build if they exist elsewhere.
// But for F303 we specifically added them in the #ifdef block above because we needed them.

#endif // else NANOVNA_F303

#ifdef ARM_MATH_CM4
// Use CORTEX M4 rbit instruction (reverse bit order in 32bit value)
static uint32_t reverse_bits(uint32_t x, int n) {
  uint32_t result;
  __asm volatile("rbit %0, %1" : "=r"(result) : "r"(x));
  return result >> (32 - n); // made shift for correct result
}
#else
static uint16_t reverse_bits(uint16_t x, int n) {
  uint16_t result = 0;
  int i;
  for (i = 0; i < n; i++, x >>= 1)
    result = (result << 1) | (x & 1U);
  return result;
}
#endif

/***
 * dir = forward: 0, inverse: 1
 * https://www.nayuki.io/res/free-small-fft-in-multiple-languages/fft.c
 */
void fft(float array[][2], const uint8_t dir) {
// FFT_SIZE = 2^FFT_N
#if FFT_SIZE == 256
#define FFT_N 8
#elif FFT_SIZE == 512
#define FFT_N 9
#elif FFT_SIZE == 1024
#define FFT_N 10
#else
#error "Need define FFT_N for this FFT size"
#endif
  const uint16_t n = FFT_SIZE;
  const uint8_t levels = FFT_N; // log2(n)
  uint16_t i, j;
  for (i = 0; i < n; i++) {
    if ((j = reverse_bits(i, levels)) > i) {
      SWAP(float, array[i][0], array[j][0]);
      SWAP(float, array[i][1], array[j][1]);
    }
  }
  const uint16_t size = 2;
  uint16_t halfsize = size / 2;
  uint16_t tablestep = n / size;
  // Cooley-Tukey decimation-in-time radix-2 FFT
  for (; tablestep; tablestep >>= 1, halfsize <<= 1) {
    for (i = 0; i < n; i += halfsize * 2) {  
      for (j = 0; j < halfsize; j++) {        
        const uint16_t k = i + j;
        const uint16_t l = k + halfsize;
        const uint16_t w_index = j * tablestep;
        const float s = dir ? FFT_SIN(w_index) : -FFT_SIN(w_index);
        const float c = FFT_COS(w_index);
        const float tpre = array[l][0] * c - array[l][1] * s;
        const float tpim = array[l][0] * s + array[l][1] * c;
        array[l][0] = array[k][0] - tpre;
        array[k][0] += tpre;
        array[l][1] = array[k][1] - tpim;
        array[k][1] += tpim;
      }
    }
  }
}

//**********************************************************************************
//      VNA math (Common)
//**********************************************************************************
// Cleanup declarations if used default math.h functions
#undef vna_sqrtf
#undef vna_cbrtf
#undef vna_logf
#undef vna_atanf
#undef vna_atan2f
#undef vna_modff

// vna_modff is defined in specific sections.

//**********************************************************************************
// square root
//**********************************************************************************
#if (__FPU_PRESENT == 0) && (__FPU_USED == 0)
#if 1
// __ieee754_sqrtf, remove some check (NAN, inf, normalization), small code optimization to arm
float vna_sqrtf(float x) {
  int32_t ix, s, q, m, t;
  uint32_t r;
  union {
    float f;
    uint32_t i;
  } u = {x};
  ix = u.i;
#if 0
  if((ix&0x7f800000)==0x7f800000) return x*x+x;
  if (ix <  0) return (x-x)/0.0f;
#endif
  if (ix == 0)
    return 0.0f;
  m = (ix >> 23);
#if 0 
  if(m==0) {				
    for(int k=0;(ix&0x00800000)==0;k++) ix<<=1;
      m -= k-1;
  }
#endif
  m -= 127; // unbias exponent
  ix = (ix & 0x007fffff) | 0x00800000;
  // generate sqrt(x) bit by bit
  ix <<= (m & 1) ? 2 : 1; 
  m >>= 1;                
  q = s = 0;              
  r = 0x01000000;         
  while (r != 0) {
    t = s + r;
    if (t <= ix) {
      s = t + r;
      ix -= t;
      q += r;
    }
    ix += ix;
    r >>= 1;
  }
  if (ix != 0) {
    if ((1.0f - 1e-30f) >= 1.0f)
      q += ((1.0f + 1e-30f) > 1.0f) ? 2 : (q & 1);
  }
  ix = (q >> 1) + 0x3f000000;
  // m can be negative for inputs below one; unsigned shifting is defined.
  ix = (int32_t)((uint32_t)ix + ((uint32_t)m << 23));
  u.i = ix;
  return u.f;
}
#else
float vna_sqrtf(float x) {
  union {
    float x;
    uint32_t i;
  } u = {x};
  u.i = (1 << 29) + (u.i >> 1) - (1 << 22);
  u.x = u.x + x / u.x;
  u.x = 0.25f * u.x + x / u.x;
  return u.x;
}
#endif
#endif

float vna_cbrtf(float x) {
#if 1
  static const uint32_t B1 = 709958130, 
      B2 = 642849266;                   
  float r, T;
  union { float f; uint32_t i; } u = {x};
  uint32_t hx = u.i & 0x7fffffff;
  if (hx < 0x00800000) { 
    if (hx == 0) return x; 
    u.f = x * 0x1p24f;
    hx = u.i & 0x7fffffff;
    hx = hx / 3 + B2;
  } else hx = hx / 3 + B1;
  u.i &= 0x80000000;
  u.i |= hx;
  T = u.f;
  r = T * T * T;
  T *= (x + x + r) / (x + r + r);
  r = T * T * T;
  T *= (x + x + r) / (x + r + r);
  return T;
#else
  if (x == 0) return 0;
  float b = 1.0f; 
  float last_b_1 = 0;
  float last_b_2 = 0;
  while (last_b_1 != b && last_b_2 != b) {
    last_b_1 = b;
    b = (2 * b + x / b / b) / 3; 
    last_b_2 = b;
    b = (2 * b + x / b / b) / 3; 
  }
  return b;
#endif
}

float vna_logf(float x) {
  const float MULTIPLIER = logf(2.0f);
#if 0
  // ...
#elif 1
  union { float f; uint32_t i; } vx = {x};
  union { uint32_t i; float f; } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  if (vx.i <= 0) return -1 / (x * x);
  return vx.i * (MULTIPLIER / (1 << 23)) - (124.22544637f * MULTIPLIER) -
         (1.498030302f * MULTIPLIER) * mx.f - (1.72587999f * MULTIPLIER) / (0.3520887068f + mx.f);
#else
  // ...
#endif
}

float vna_log10f_x_10(float x) {
  const float MULTIPLIER = (10.0f * logf(2.0f) / logf(10.0f));
#if 0
  // ...
#else
  union { float f; uint32_t i; } vx = {x};
  union { uint32_t i; float f; } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  if (vx.i <= 0) return -1 / (x * x);
  return vx.i * (MULTIPLIER / (1 << 23)) - (124.22544637f * MULTIPLIER) -
         (1.498030302f * MULTIPLIER) * mx.f - (1.72587999f * MULTIPLIER) / (0.3520887068f + mx.f);
#endif
}

float vna_atanf(float x) {
  static const float atanhi[] = {
      4.6364760399e-01, 7.8539812565e-01, 9.8279368877e-01, 1.5707962513e+00,
  };
  static const float atanlo[] = {
      5.0121582440e-09, 3.7748947079e-08, 3.4473217170e-08, 7.5497894159e-08,
  };
  static const float aT[] = {
      3.3333328366e-01, -1.9999158382e-01, 1.4253635705e-01, -1.0648017377e-01, 6.1687607318e-02,
  };
  float w, s1, s2, z;
  uint32_t ix, sign;
  int id;
  union { float f; uint32_t i; } u = {x};
  ix = u.i;
  sign = ix >> 31;
  ix &= 0x7fffffff;
  if (ix >= 0x4c800000) { 
    if (ix > 0x7f800000) return x;
    z = atanhi[3] + 0x1p-120f;
    return sign ? -z : z;
  }
  if (ix < 0x3ee00000) {   
    if (ix < 0x39800000) return x;
    id = -1;
  } else {
    x = vna_fabsf(x);
    if (ix < 0x3f980000) {   
      if (ix < 0x3f300000) { id = 0; x = (2.0f * x - 1.0f) / (2.0f + x); } 
      else { id = 1; x = (x - 1.0f) / (x + 1.0f); }
    } else {
      if (ix < 0x401c0000) { id = 2; x = (x - 1.5f) / (1.0f + 1.5f * x); } 
      else { id = 3; x = -1.0f / x; }
    }
  }
  z = x * x;
  w = z * z;
  s1 = z * (aT[0] + w * (aT[2] + w * aT[4]));
  s2 = w * (aT[1] + w * aT[3]);
  if (id < 0) return x - x * (s1 + s2);
  z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
  return sign ? -z : z;
}

// Reference vna_atan2f (Common)
float vna_atan2f(float y, float x)
{
  union {float f; int32_t i;} ux = {x};
  union {float f; int32_t i;} uy = {y};
  if (ux.i == 0 && uy.i == 0)
    return 0.0f;

  float ax, ay, r, s;
  ax = vna_fabsf(x);
  ay = vna_fabsf(y);
  r = (ay < ax) ? ay / ax : ax / ay;
  s = r * r;
  
  // give 0.005 degree error
  r*= 0.999133448222780f - s * (0.320533292381664f - s * (0.144982490144465f - s * 0.038254464970299f));

  // Map to full circle
  if (ay  > ax) r = VNA_PI/2.0f - r;
  if (ux.i < 0) r = VNA_PI      - r;
  if (uy.i < 0) r = -r;
  return r;
}

// Reference vna_expf (Common)
float vna_expf(float x)
{
  union { float f; int32_t i; } v;
  v.i = (int32_t)(12102203.0f*x) + 0x3F800000;
  int32_t m = (v.i >> 7) & 0xFFFF;  // copy mantissa
  // cubic spline approximation, empirical values for small maximum relative error (8.34e-5):
  v.i += ((((((((1277*m) >> 14) + 14825)*m) >> 14) - 79749)*m) >> 11) - 626;
  return v.f;
}

