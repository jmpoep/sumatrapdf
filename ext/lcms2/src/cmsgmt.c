//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2021 Marti Maria Saguer
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//---------------------------------------------------------------------------------
//

#include "lcms2_internal.h"


// Auxiliary: append a Lab identity after the given sequence of profiles
// and return the transform. Lab profile is closed, rest of profiles are kept open.
cmsHTRANSFORM _cmsChain2Lab(cmsContext            ContextID,
                            cmsUInt32Number        nProfiles,
                            cmsUInt32Number        InputFormat,
                            cmsUInt32Number        OutputFormat,
                            const cmsUInt32Number  Intents[],
                            const cmsHPROFILE      hProfiles[],
                            const cmsBool          BPC[],
                            const cmsFloat64Number AdaptationStates[],
                            cmsUInt32Number        dwFlags)
{
    cmsHTRANSFORM xform;
    cmsHPROFILE   hLab;
    cmsHPROFILE   ProfileList[256];
    cmsBool       BPCList[256];
    cmsFloat64Number AdaptationList[256];
    cmsUInt32Number IntentList[256];
    cmsUInt32Number i;

    // This is a rather big number and there is no need of dynamic memory
    // since we are adding a profile, 254 + 1 = 255 and this is the limit
    if (nProfiles > 254) return NULL;

    // The output space
    hLab = cmsCreateLab4Profile(ContextID, NULL);
    if (hLab == NULL) return NULL;

    // Create a copy of parameters
    for (i=0; i < nProfiles; i++) {

        ProfileList[i]    = hProfiles[i];
        BPCList[i]        = BPC[i];
        AdaptationList[i] = AdaptationStates[i];
        IntentList[i]     = Intents[i];
    }

    // Place Lab identity at chain's end.
    ProfileList[nProfiles]    = hLab;
    BPCList[nProfiles]        = 0;
    AdaptationList[nProfiles] = 1.0;
    IntentList[nProfiles]     = INTENT_RELATIVE_COLORIMETRIC;

    // Create the transform
    xform = cmsCreateExtendedTransform(ContextID, nProfiles + 1, ProfileList,
                                       BPCList,
                                       IntentList,
                                       AdaptationList,
                                       NULL, 0,
                                       InputFormat,
                                       OutputFormat,
                                       dwFlags);

    cmsCloseProfile(ContextID, hLab);

    return xform;
}


// Compute K -> L* relationship. Flags may include black point compensation. In this case,
// the relationship is assumed from the profile with BPC to a black point zero.
static
cmsToneCurve* ComputeKToLstar(cmsContext            ContextID,
                               cmsUInt32Number       nPoints,
                               cmsUInt32Number       nProfiles,
                               const cmsUInt32Number Intents[],
                               const cmsHPROFILE     hProfiles[],
                               const cmsBool         BPC[],
                               const cmsFloat64Number AdaptationStates[],
                               cmsUInt32Number dwFlags)
{
    cmsToneCurve* out = NULL;
    cmsUInt32Number i;
    cmsHTRANSFORM xform;
    cmsCIELab Lab;
    cmsFloat32Number cmyk[4];
    cmsFloat32Number* SampledPoints;

    xform = _cmsChain2Lab(ContextID, nProfiles, TYPE_CMYK_FLT, TYPE_Lab_DBL, Intents, hProfiles, BPC, AdaptationStates, dwFlags);
    if (xform == NULL) return NULL;

    SampledPoints = (cmsFloat32Number*) _cmsCalloc(ContextID, nPoints, sizeof(cmsFloat32Number));
    if (SampledPoints  == NULL) goto Error;

    for (i=0; i < nPoints; i++) {

        cmyk[0] = 0;
        cmyk[1] = 0;
        cmyk[2] = 0;
        cmyk[3] = (cmsFloat32Number) ((i * 100.0) / (nPoints-1));

        cmsDoTransform(ContextID, xform, cmyk, &Lab, 1);
        SampledPoints[i]= (cmsFloat32Number) (1.0 - Lab.L / 100.0); // Negate K for easier operation
    }

    out = cmsBuildTabulatedToneCurveFloat(ContextID, nPoints, SampledPoints);

Error:

    cmsDeleteTransform(ContextID, xform);
    if (SampledPoints) _cmsFree(ContextID, SampledPoints);

    return out;
}


// Compute Black tone curve on a CMYK -> CMYK transform. This is done by
// using the proof direction on both profiles to find K->L* relationship
// then joining both curves. dwFlags may include black point compensation.
cmsToneCurve* _cmsBuildKToneCurve(cmsContext        ContextID,
                                   cmsUInt32Number   nPoints,
                                   cmsUInt32Number   nProfiles,
                                   const cmsUInt32Number Intents[],
                                   const cmsHPROFILE hProfiles[],
                                   const cmsBool     BPC[],
                                   const cmsFloat64Number AdaptationStates[],
                                   cmsUInt32Number   dwFlags)
{
    cmsToneCurve *in, *out, *KTone;

    // Make sure CMYK -> CMYK
    if (cmsGetColorSpace(ContextID, hProfiles[0]) != cmsSigCmykData ||
        cmsGetColorSpace(ContextID, hProfiles[nProfiles-1])!= cmsSigCmykData) return NULL;


    // Make sure last is an output profile
    if (cmsGetDeviceClass(ContextID, hProfiles[nProfiles - 1]) != cmsSigOutputClass) return NULL;

    // Create individual curves. BPC works also as each K to L* is
    // computed as a BPC to zero black point in case of L*
    in  = ComputeKToLstar(ContextID, nPoints, nProfiles - 1, Intents, hProfiles, BPC, AdaptationStates, dwFlags);
    if (in == NULL) return NULL;

    out = ComputeKToLstar(ContextID, nPoints, 1,
                            Intents + (nProfiles - 1),
                            &hProfiles [nProfiles - 1],
                            BPC + (nProfiles - 1),
                            AdaptationStates + (nProfiles - 1),
                            dwFlags);
    if (out == NULL) {
        cmsFreeToneCurve(ContextID, in);
        return NULL;
    }

    // Build the relationship. This effectively limits the maximum accuracy to 16 bits, but
    // since this is used on black-preserving LUTs, we are not losing  accuracy in any case
    KTone = cmsJoinToneCurve(ContextID, in, out, nPoints);

    // Get rid of components
    cmsFreeToneCurve(ContextID, in); cmsFreeToneCurve(ContextID, out);

    // Something went wrong...
    if (KTone == NULL) return NULL;

    // Make sure it is monotonic
    if (!cmsIsToneCurveMonotonic(ContextID, KTone)) {
        cmsFreeToneCurve(ContextID, KTone);
        return NULL;
    }

    return KTone;
}


// Gamut LUT Creation -----------------------------------------------------------------------------------------

// Used by gamut & softproofing

typedef struct {

    cmsHTRANSFORM hInput;               // From whatever input color space. 16 bits to DBL
    cmsHTRANSFORM hForward, hReverse;   // Transforms going from Lab to colorant and back
    cmsFloat64Number Threshold;         // The threshold after which is considered out of gamut

    } GAMUTCHAIN;

// This sampler does compute gamut boundaries by comparing original
// values with a transform going back and forth. Values above ERR_THRESHOLD
// of maximum are considered out of gamut.

#define ERR_THRESHOLD      5


static
int GamutSampler(cmsContext ContextID, CMSREGISTER const cmsUInt16Number In[], CMSREGISTER cmsUInt16Number Out[], CMSREGISTER void* Cargo)
{
    GAMUTCHAIN*  t = (GAMUTCHAIN* ) Cargo;
    cmsCIELab LabIn1, LabOut1;
    cmsCIELab LabIn2, LabOut2;
    cmsUInt16Number Proof[cmsMAXCHANNELS], Proof2[cmsMAXCHANNELS];
    cmsFloat64Number dE1, dE2, ErrorRatio;

    // Assume in-gamut by default. NEVER READ, USED FOR DEBUG PURPOSES.
    ErrorRatio = 1.0;

    // Convert input to Lab
    cmsDoTransform(ContextID, t -> hInput, In, &LabIn1, 1);

    // converts from PCS to colorant. This always
    // does return in-gamut values,
    cmsDoTransform(ContextID, t -> hForward, &LabIn1, Proof, 1);

    // Now, do the inverse, from colorant to PCS.
    cmsDoTransform(ContextID, t -> hReverse, Proof, &LabOut1, 1);

    memmove(&LabIn2, &LabOut1, sizeof(cmsCIELab));

    // Try again, but this time taking Check as input
    cmsDoTransform(ContextID, t -> hForward, &LabOut1, Proof2, 1);
    cmsDoTransform(ContextID, t -> hReverse, Proof2, &LabOut2, 1);

    // Take difference of direct value
    dE1 = cmsDeltaE(ContextID, &LabIn1, &LabOut1);

    // Take difference of converted value
    dE2 = cmsDeltaE(ContextID, &LabIn2, &LabOut2);


    // if dE1 is small and dE2 is small, value is likely to be in gamut
    if (dE1 < t->Threshold && dE2 < t->Threshold)
        Out[0] = 0;
    else {

        // if dE1 is small and dE2 is big, undefined. Assume in gamut
        if (dE1 < t->Threshold && dE2 > t->Threshold)
            Out[0] = 0;
        else
            // dE1 is big and dE2 is small, clearly out of gamut
            if (dE1 > t->Threshold && dE2 < t->Threshold)
                Out[0] = (cmsUInt16Number) _cmsQuickFloor((dE1 - t->Threshold) + .5);
            else  {

                // dE1 is big and dE2 is also big, could be due to perceptual mapping
                // so take error ratio
                if (dE2 == 0.0)
                    ErrorRatio = dE1;
                else
                    ErrorRatio = dE1 / dE2;

                if (ErrorRatio > t->Threshold)
                    Out[0] = (cmsUInt16Number)  _cmsQuickFloor((ErrorRatio - t->Threshold) + .5);
                else
                    Out[0] = 0;
            }
    }


    return TRUE;
}

// Does compute a gamut LUT going back and forth across pcs -> relativ. colorimetric intent -> pcs
// the dE obtained is then annotated on the LUT. Values truly out of gamut are clipped to dE = 0xFFFE
// and values changed are supposed to be handled by any gamut remapping, so, are out of gamut as well.
//
// **WARNING: This algorithm does assume that gamut remapping algorithms does NOT move in-gamut colors,
// of course, many perceptual and saturation intents does not work in such way, but relativ. ones should.

cmsPipeline* _cmsCreateGamutCheckPipeline(cmsContext ContextID,
                                          cmsHPROFILE hProfiles[],
                                          cmsBool  BPC[],
                                          cmsUInt32Number Intents[],
                                          cmsFloat64Number AdaptationStates[],
                                          cmsUInt32Number nGamutPCSposition,
                                          cmsHPROFILE hGamut)
{
    cmsHPROFILE hLab;
    cmsPipeline* Gamut;
    cmsStage* CLUT;
    cmsUInt32Number dwFormat;
    GAMUTCHAIN Chain;
    cmsUInt32Number nGridpoints;
    cmsInt32Number nChannels;
    cmsColorSpaceSignature ColorSpace;
    cmsUInt32Number i;
    cmsHPROFILE ProfileList[256];
    cmsBool     BPCList[256];
    cmsFloat64Number AdaptationList[256];
    cmsUInt32Number IntentList[256];

    memset(&Chain, 0, sizeof(GAMUTCHAIN));


    if (nGamutPCSposition <= 0 || nGamutPCSposition > 255) {
        cmsSignalError(ContextID, cmsERROR_RANGE, "Wrong position of PCS. 1..255 expected, %d found.", nGamutPCSposition);
        return NULL;
    }

    hLab = cmsCreateLab4Profile(ContextID, NULL);
    if (hLab == NULL) return NULL;


    // The figure of merit. On matrix-shaper profiles, should be almost zero as
    // the conversion is pretty exact. On LUT based profiles, different resolutions
    // of input and output CLUT may result in differences.

    if (cmsIsMatrixShaper(ContextID, hGamut)) {

        Chain.Threshold = 1.0;
    }
    else {
        Chain.Threshold = ERR_THRESHOLD;
    }


    // Create a copy of parameters
    for (i=0; i < nGamutPCSposition; i++) {
        ProfileList[i]    = hProfiles[i];
        BPCList[i]        = BPC[i];
        AdaptationList[i] = AdaptationStates[i];
        IntentList[i]     = Intents[i];
    }

    // Fill Lab identity
    ProfileList[nGamutPCSposition] = hLab;
    BPCList[nGamutPCSposition] = 0;
    AdaptationList[nGamutPCSposition] = 1.0;
    IntentList[nGamutPCSposition] = INTENT_RELATIVE_COLORIMETRIC;


    ColorSpace  = cmsGetColorSpace(ContextID, hGamut);
    nChannels   = cmsChannelsOfColorSpace(ContextID, ColorSpace);
    nGridpoints = _cmsReasonableGridpointsByColorspace(ContextID, ColorSpace, cmsFLAGS_HIGHRESPRECALC);
    dwFormat    = (CHANNELS_SH(nChannels)|BYTES_SH(2));

    // 16 bits to Lab double
    Chain.hInput = cmsCreateExtendedTransform(ContextID,
        nGamutPCSposition + 1,
        ProfileList,
        BPCList,
        IntentList,
        AdaptationList,
        NULL, 0,
        dwFormat, TYPE_Lab_DBL,
        cmsFLAGS_NOCACHE);


    // Does create the forward step. Lab double to device
    dwFormat    = (CHANNELS_SH(nChannels)|BYTES_SH(2));
    Chain.hForward = cmsCreateTransform(ContextID,
        hLab, TYPE_Lab_DBL,
        hGamut, dwFormat,
        INTENT_RELATIVE_COLORIMETRIC,
        cmsFLAGS_NOCACHE);

    // Does create the backwards step
    Chain.hReverse = cmsCreateTransform(ContextID, hGamut, dwFormat,
        hLab, TYPE_Lab_DBL,
        INTENT_RELATIVE_COLORIMETRIC,
        cmsFLAGS_NOCACHE);


    // All ok?
    if (Chain.hInput && Chain.hForward && Chain.hReverse) {

        // Go on, try to compute gamut LUT from PCS. This consist on a single channel containing
        // dE when doing a transform back and forth on the colorimetric intent.

        Gamut = cmsPipelineAlloc(ContextID, 3, 1);
        if (Gamut != NULL) {

            CLUT = cmsStageAllocCLut16bit(ContextID, nGridpoints, nChannels, 1, NULL);
            if (!cmsPipelineInsertStage(ContextID, Gamut, cmsAT_BEGIN, CLUT)) {
                cmsPipelineFree(ContextID, Gamut);
                Gamut = NULL;
            }
            else {
                cmsStageSampleCLut16bit(ContextID, CLUT, GamutSampler, (void*) &Chain, 0);
            }
        }
    }
    else
        Gamut = NULL;   // Didn't work...

    // Free all needed stuff.
    if (Chain.hInput)   cmsDeleteTransform(ContextID, Chain.hInput);
    if (Chain.hForward) cmsDeleteTransform(ContextID, Chain.hForward);
    if (Chain.hReverse) cmsDeleteTransform(ContextID, Chain.hReverse);
    if (hLab) cmsCloseProfile(ContextID, hLab);

    // And return computed hull
    return Gamut;
}

// Total Area Coverage estimation ----------------------------------------------------------------

typedef struct {
    cmsUInt32Number  nOutputChans;
    cmsHTRANSFORM    hRoundTrip;
    cmsFloat32Number MaxTAC;
    cmsFloat32Number MaxInput[cmsMAXCHANNELS];

} cmsTACestimator;


// This callback just accounts the maximum ink dropped in the given node. It does not populate any
// memory, as the destination table is NULL. Its only purpose it to know the global maximum.
static
int EstimateTAC(cmsContext ContextID, CMSREGISTER const cmsUInt16Number In[], CMSREGISTER cmsUInt16Number Out[], CMSREGISTER void * Cargo)
{
    cmsTACestimator* bp = (cmsTACestimator*) Cargo;
    cmsFloat32Number RoundTrip[cmsMAXCHANNELS];
    cmsUInt32Number i;
    cmsFloat32Number Sum;


    // Evaluate the xform
    cmsDoTransform(ContextID, bp->hRoundTrip, In, RoundTrip, 1);

    // All all amounts of ink
    for (Sum=0, i=0; i < bp ->nOutputChans; i++)
            Sum += RoundTrip[i];

    // If above maximum, keep track of input values
    if (Sum > bp ->MaxTAC) {

            bp ->MaxTAC = Sum;

            for (i=0; i < bp ->nOutputChans; i++) {
                bp ->MaxInput[i] = In[i];
            }
    }

    return TRUE;

    cmsUNUSED_PARAMETER(Out);
}


// Detect Total area coverage of the profile
cmsFloat64Number CMSEXPORT cmsDetectTAC(cmsContext ContextID, cmsHPROFILE hProfile)
{
    cmsTACestimator bp;
    cmsUInt32Number dwFormatter;
    cmsUInt32Number GridPoints[MAX_INPUT_DIMENSIONS];
    cmsHPROFILE hLab;

    // TAC only works on output profiles
    if (cmsGetDeviceClass(ContextID, hProfile) != cmsSigOutputClass) {
        return 0;
    }

    // Create a fake formatter for result
    dwFormatter = cmsFormatterForColorspaceOfProfile(ContextID, hProfile, 4, TRUE);

    // Unsupported color space?
    if (dwFormatter == 0) return 0;

    bp.nOutputChans = T_CHANNELS(dwFormatter);
    bp.MaxTAC = 0;    // Initial TAC is 0

    //  for safety
    if (bp.nOutputChans >= cmsMAXCHANNELS) return 0;

    hLab = cmsCreateLab4Profile(ContextID, NULL);
    if (hLab == NULL) return 0;
    // Setup a roundtrip on perceptual intent in output profile for TAC estimation
    bp.hRoundTrip = cmsCreateTransform(ContextID, hLab, TYPE_Lab_16,
                                          hProfile, dwFormatter, INTENT_PERCEPTUAL, cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);

    cmsCloseProfile(ContextID, hLab);
    if (bp.hRoundTrip == NULL) return 0;

    // For L* we only need black and white. For C* we need many points
    GridPoints[0] = 6;
    GridPoints[1] = 74;
    GridPoints[2] = 74;


    if (!cmsSliceSpace16(ContextID, 3, GridPoints, EstimateTAC, &bp)) {
        bp.MaxTAC = 0;
    }

    cmsDeleteTransform(ContextID, bp.hRoundTrip);

    // Results in %
    return bp.MaxTAC;
}


// Carefully,  clamp on CIELab space.

cmsBool CMSEXPORT cmsDesaturateLab(cmsContext ContextID, cmsCIELab* Lab,
                                   double amax, double amin,
                                   double bmax, double bmin)
{

    // Whole Luma surface to zero

    if (Lab -> L < 0) {

        Lab-> L = Lab->a = Lab-> b = 0.0;
        return FALSE;
    }

    // Clamp white, DISCARD HIGHLIGHTS. This is done
    // in such way because icc spec doesn't allow the
    // use of L>100 as a highlight means.

    if (Lab->L > 100)
        Lab -> L = 100;

    // Check out gamut prism, on a, b faces

    if (Lab -> a < amin || Lab->a > amax||
        Lab -> b < bmin || Lab->b > bmax) {

            cmsCIELCh LCh;
            double h, slope;

            // Falls outside a, b limits. Transports to LCh space,
            // and then do the clipping


            if (Lab -> a == 0.0) { // Is hue exactly 90?

                // atan will not work, so clamp here
                Lab -> b = Lab->b < 0 ? bmin : bmax;
                return TRUE;
            }

            cmsLab2LCh(ContextID, &LCh, Lab);

            slope = Lab -> b / Lab -> a;
            h = LCh.h;

            // There are 4 zones

            if ((h >= 0. && h < 45.) ||
                (h >= 315 && h <= 360.)) {

                    // clip by amax
                    Lab -> a = amax;
                    Lab -> b = amax * slope;
            }
            else
                if (h >= 45. && h < 135.)
                {
                    // clip by bmax
                    Lab -> b = bmax;
                    Lab -> a = bmax / slope;
                }
                else
                    if (h >= 135. && h < 225.) {
                        // clip by amin
                        Lab -> a = amin;
                        Lab -> b = amin * slope;

                    }
                    else
                        if (h >= 225. && h < 315.) {
                            // clip by bmin
                            Lab -> b = bmin;
                            Lab -> a = bmin / slope;
                        }
                        else  {
                            cmsSignalError(0, cmsERROR_RANGE, "Invalid angle");
                            return FALSE;
                        }

    }

    return TRUE;
}

// Detect whatever a given ICC profile works in linear (gamma 1.0) space
// Actually, doing that "well" is quite hard, since every component may behave completely different.
// Since the true point of this function is to detect suitable optimizations, I am imposing some requirements
// that simplifies things: only RGB, and only profiles that can got in both directions.
// The algorithm obtains Y from a synthetical gray R=G=B. Then least squares fitting is used to estimate gamma.
// For gamma close to 1.0, RGB is linear. On profiles not supported, -1 is returned.

cmsFloat64Number CMSEXPORT cmsDetectRGBProfileGamma(cmsContext ContextID, cmsHPROFILE hProfile, cmsFloat64Number threshold)
{
    cmsHPROFILE hXYZ;
    cmsHTRANSFORM xform;
    cmsToneCurve* Y_curve;
    cmsUInt16Number rgb[256][3];
    cmsCIEXYZ XYZ[256];
    cmsFloat32Number Y_normalized[256];
    cmsFloat64Number gamma;
    cmsProfileClassSignature cl;
    int i;

    if (cmsGetColorSpace(ContextID, hProfile) != cmsSigRgbData)
        return -1;

    cl = cmsGetDeviceClass(ContextID, hProfile);
    if (cl != cmsSigInputClass && cl != cmsSigDisplayClass &&
        cl != cmsSigOutputClass && cl != cmsSigColorSpaceClass)
        return -1;

    hXYZ = cmsCreateXYZProfile(ContextID);
    if (hXYZ == NULL)
        return -1;
    xform = cmsCreateTransform(ContextID, hProfile, TYPE_RGB_16, hXYZ, TYPE_XYZ_DBL,
                                    INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);

    if (xform == NULL) { // If not RGB or forward direction is not supported, regret with the previous error

        cmsCloseProfile(ContextID, hXYZ);
        return -1;
    }

    for (i = 0; i < 256; i++) {
        rgb[i][0] = rgb[i][1] = rgb[i][2] = FROM_8_TO_16(i);
    }

    cmsDoTransform(ContextID, xform, rgb, XYZ, 256);

    cmsDeleteTransform(ContextID, xform);
    cmsCloseProfile(ContextID, hXYZ);

    for (i = 0; i < 256; i++) {
        Y_normalized[i] = (cmsFloat32Number) XYZ[i].Y;
    }

    Y_curve = cmsBuildTabulatedToneCurveFloat(ContextID, 256, Y_normalized);
    if (Y_curve == NULL)
        return -1;

    gamma = cmsEstimateGamma(ContextID, Y_curve, threshold);

    cmsFreeToneCurve(ContextID, Y_curve);

    return gamma;
}
