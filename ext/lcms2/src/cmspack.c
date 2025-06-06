//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2023 Marti Maria Saguer
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

// This module handles all formats supported by lcms. There are two flavors, 16 bits and
// floating point. Floating point is supported only in a subset, those formats holding
// cmsFloat32Number (4 bytes per component) and double (marked as 0 bytes per component
// as special case)

// ---------------------------------------------------------------------------


// This macro return words stored as big endian
#define CHANGE_ENDIAN(w)    (cmsUInt16Number) ((cmsUInt16Number) ((w)<<8)|((w)>>8))

// These macros handles reversing (negative)
#define REVERSE_FLAVOR_8(x)     ((cmsUInt8Number) (0xff-(x)))
#define REVERSE_FLAVOR_16(x)    ((cmsUInt16Number)(0xffff-(x)))

// * 0xffff / 0xff00 = (255 * 257) / (255 * 256) = 257 / 256
cmsINLINE cmsUInt16Number FomLabV2ToLabV4(cmsUInt16Number x)
{
    int a = (x << 8 | x) >> 8;  // * 257 / 256
    if ( a > 0xffff) return 0xffff;
    return (cmsUInt16Number) a;
}

// * 0xf00 / 0xffff = * 256 / 257
cmsINLINE cmsUInt16Number FomLabV4ToLabV2(cmsUInt16Number x)
{
    return (cmsUInt16Number) (((x << 8) + 0x80) / 257);
}


typedef struct {
    cmsUInt32Number Type;
    cmsUInt32Number Mask;
    cmsFormatter16  Frm;

} cmsFormatters16;

typedef struct {
    cmsUInt32Number    Type;
    cmsUInt32Number    Mask;
    cmsFormatterFloat  Frm;

} cmsFormattersFloat;


#define ANYSPACE        COLORSPACE_SH(31)
#define ANYCHANNELS     CHANNELS_SH(15)
#define ANYEXTRA        EXTRA_SH(63)
#define ANYPLANAR       PLANAR_SH(1)
#define ANYENDIAN       ENDIAN16_SH(1)
#define ANYSWAP         DOSWAP_SH(1)
#define ANYSWAPFIRST    SWAPFIRST_SH(1)
#define ANYFLAVOR       FLAVOR_SH(1)
#define ANYPREMUL       PREMUL_SH(1)


// Suppress waning about info never being used

#ifdef _MSC_VER
#pragma warning(disable : 4100)
#endif

// Unpacking routines (16 bits) ----------------------------------------------------------------------------------------


// Does almost everything but is slow
static
cmsUInt8Number* UnrollChunkyBytes(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wIn[],
                                  CMSREGISTER cmsUInt8Number* accum,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan      = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap     = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> InputFormat);
    cmsUInt32Number Extra      = T_EXTRA(info -> InputFormat);
    cmsUInt32Number Premul     = T_PREMUL(info->InputFormat);

    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number v;
    cmsUInt32Number i;
    cmsUInt32Number alpha_factor = 1;

    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(accum[0]));

        accum += Extra;
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(accum[nChan]));
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = FROM_8_TO_16(*accum);
        v = Reverse ? REVERSE_FLAVOR_16(v) : v;

        if (Premul && alpha_factor > 0)
        {
            v = ((cmsUInt32Number)((cmsUInt32Number)v << 16) / alpha_factor);
            if (v > 0xffff) v = 0xffff;
        }

        wIn[index] = (cmsUInt16Number) v;
        accum++;
    }

    if (!ExtraFirst) {
        accum += Extra;
    }

    if (Extra == 0 && SwapFirst) {
        cmsUInt16Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsUInt16Number));
        wIn[nChan-1] = tmp;
    }

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);

}


// Extra channels are just ignored because come in the next planes
static
cmsUInt8Number* UnrollPlanarBytes(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wIn[],
                                  CMSREGISTER cmsUInt8Number* accum,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan     = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap    = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info ->InputFormat);
    cmsUInt32Number Reverse   = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number i;
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Extra = T_EXTRA(info->InputFormat);
    cmsUInt32Number Premul = T_PREMUL(info->InputFormat);
    cmsUInt8Number* Init = accum;
    cmsUInt32Number alpha_factor = 1;

    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(accum[0]));


        accum += Extra * Stride;
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(accum[(nChan) * Stride]));
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt32Number v = FROM_8_TO_16(*accum);

        v = Reverse ? REVERSE_FLAVOR_16(v) : v;

        if (Premul && alpha_factor > 0)
        {
            v = ((cmsUInt32Number)((cmsUInt32Number)v << 16) / alpha_factor);
            if (v > 0xffff) v = 0xffff;
        }

        wIn[index] = (cmsUInt16Number) v;
        accum += Stride;
    }

    return (Init + 1);
}


// Special cases, provided for performance
static
cmsUInt8Number* Unroll4Bytes(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FROM_8_TO_16(*accum); accum++; // C
    wIn[1] = FROM_8_TO_16(*accum); accum++; // M
    wIn[2] = FROM_8_TO_16(*accum); accum++; // Y
    wIn[3] = FROM_8_TO_16(*accum); accum++; // K

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4BytesReverse(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FROM_8_TO_16(REVERSE_FLAVOR_8(*accum)); accum++; // C
    wIn[1] = FROM_8_TO_16(REVERSE_FLAVOR_8(*accum)); accum++; // M
    wIn[2] = FROM_8_TO_16(REVERSE_FLAVOR_8(*accum)); accum++; // Y
    wIn[3] = FROM_8_TO_16(REVERSE_FLAVOR_8(*accum)); accum++; // K

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4BytesSwapFirst(cmsContext ContextID,
                                      CMSREGISTER _cmsTRANSFORM* info,
                                      CMSREGISTER cmsUInt16Number wIn[],
                                      CMSREGISTER cmsUInt8Number* accum,
                                      CMSREGISTER cmsUInt32Number Stride)
{
    wIn[3] = FROM_8_TO_16(*accum); accum++; // K
    wIn[0] = FROM_8_TO_16(*accum); accum++; // C
    wIn[1] = FROM_8_TO_16(*accum); accum++; // M
    wIn[2] = FROM_8_TO_16(*accum); accum++; // Y

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// KYMC
static
cmsUInt8Number* Unroll4BytesSwap(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[3] = FROM_8_TO_16(*accum); accum++;  // K
    wIn[2] = FROM_8_TO_16(*accum); accum++;  // Y
    wIn[1] = FROM_8_TO_16(*accum); accum++;  // M
    wIn[0] = FROM_8_TO_16(*accum); accum++;  // C

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4BytesSwapSwapFirst(cmsContext ContextID,
                                          CMSREGISTER _cmsTRANSFORM* info,
                                          CMSREGISTER cmsUInt16Number wIn[],
                                          CMSREGISTER cmsUInt8Number* accum,
                                          CMSREGISTER cmsUInt32Number Stride)
{
    wIn[2] = FROM_8_TO_16(*accum); accum++;  // K
    wIn[1] = FROM_8_TO_16(*accum); accum++;  // Y
    wIn[0] = FROM_8_TO_16(*accum); accum++;  // M
    wIn[3] = FROM_8_TO_16(*accum); accum++;  // C

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3Bytes(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FROM_8_TO_16(*accum); accum++;     // R
    wIn[1] = FROM_8_TO_16(*accum); accum++;     // G
    wIn[2] = FROM_8_TO_16(*accum); accum++;     // B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3BytesSkip1Swap(cmsContext ContextID,
                                      CMSREGISTER _cmsTRANSFORM* info,
                                      CMSREGISTER cmsUInt16Number wIn[],
                                      CMSREGISTER cmsUInt8Number* accum,
                                      CMSREGISTER cmsUInt32Number Stride)
{
    accum++; // A
    wIn[2] = FROM_8_TO_16(*accum); accum++; // B
    wIn[1] = FROM_8_TO_16(*accum); accum++; // G
    wIn[0] = FROM_8_TO_16(*accum); accum++; // R

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3BytesSkip1SwapSwapFirst(cmsContext ContextID,
                                               CMSREGISTER _cmsTRANSFORM* info,
                                               CMSREGISTER cmsUInt16Number wIn[],
                                               CMSREGISTER cmsUInt8Number* accum,
                                               CMSREGISTER cmsUInt32Number Stride)
{
    wIn[2] = FROM_8_TO_16(*accum); accum++; // B
    wIn[1] = FROM_8_TO_16(*accum); accum++; // G
    wIn[0] = FROM_8_TO_16(*accum); accum++; // R
    accum++; // A

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3BytesSkip1SwapFirst(cmsContext ContextID,
                                           CMSREGISTER _cmsTRANSFORM* info,
                                           CMSREGISTER cmsUInt16Number wIn[],
                                           CMSREGISTER cmsUInt8Number* accum,
                                           CMSREGISTER cmsUInt32Number Stride)
{
    accum++; // A
    wIn[0] = FROM_8_TO_16(*accum); accum++; // R
    wIn[1] = FROM_8_TO_16(*accum); accum++; // G
    wIn[2] = FROM_8_TO_16(*accum); accum++; // B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


// BRG
static
cmsUInt8Number* Unroll3BytesSwap(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[2] = FROM_8_TO_16(*accum); accum++;     // B
    wIn[1] = FROM_8_TO_16(*accum); accum++;     // G
    wIn[0] = FROM_8_TO_16(*accum); accum++;     // R

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* UnrollLabV2_8(cmsContext ContextID,
                              CMSREGISTER _cmsTRANSFORM* info,
                              CMSREGISTER cmsUInt16Number wIn[],
                              CMSREGISTER cmsUInt8Number* accum,
                              CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // L
    wIn[1] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // a
    wIn[2] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // b

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* UnrollALabV2_8(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wIn[],
                               CMSREGISTER cmsUInt8Number* accum,
                               CMSREGISTER cmsUInt32Number Stride)
{
    accum++;  // A
    wIn[0] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // L
    wIn[1] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // a
    wIn[2] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // b

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* UnrollLabV2_16(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wIn[],
                               CMSREGISTER cmsUInt8Number* accum,
                               CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // L
    wIn[1] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // a
    wIn[2] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // b

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// for duplex
static
cmsUInt8Number* Unroll2Bytes(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = FROM_8_TO_16(*accum); accum++;     // ch1
    wIn[1] = FROM_8_TO_16(*accum); accum++;     // ch2

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}




// Monochrome duplicates L into RGB for null-transforms
static
cmsUInt8Number* Unroll1Byte(cmsContext ContextID,
                            CMSREGISTER _cmsTRANSFORM* info,
                            CMSREGISTER cmsUInt16Number wIn[],
                            CMSREGISTER cmsUInt8Number* accum,
                            CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = FROM_8_TO_16(*accum); accum++;     // L

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Unroll1ByteSkip1(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = FROM_8_TO_16(*accum); accum++;     // L
    accum += 1;

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll1ByteSkip2(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = FROM_8_TO_16(*accum); accum++;     // L
    accum += 2;

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll1ByteReversed(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = REVERSE_FLAVOR_16(FROM_8_TO_16(*accum)); accum++;     // L

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* UnrollAnyWords(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wIn[],
                               CMSREGISTER cmsUInt8Number* accum,
                               CMSREGISTER cmsUInt32Number Stride)
{
   cmsUInt32Number nChan       = T_CHANNELS(info -> InputFormat);
   cmsUInt32Number SwapEndian  = T_ENDIAN16(info -> InputFormat);
   cmsUInt32Number DoSwap      = T_DOSWAP(info ->InputFormat);
   cmsUInt32Number Reverse     = T_FLAVOR(info ->InputFormat);
   cmsUInt32Number SwapFirst   = T_SWAPFIRST(info -> InputFormat);
   cmsUInt32Number Extra       = T_EXTRA(info -> InputFormat);
   cmsUInt32Number ExtraFirst  = DoSwap ^ SwapFirst;
   cmsUInt32Number i;

    if (ExtraFirst) {
        accum += Extra * sizeof(cmsUInt16Number);
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt16Number v = *(cmsUInt16Number*) accum;

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        wIn[index] = Reverse ? REVERSE_FLAVOR_16(v) : v;

        accum += sizeof(cmsUInt16Number);
    }

    if (!ExtraFirst) {
        accum += Extra * sizeof(cmsUInt16Number);
    }

    if (Extra == 0 && SwapFirst) {

        cmsUInt16Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsUInt16Number));
        wIn[nChan-1] = tmp;
    }

    return accum;

    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* UnrollAnyWordsPremul(cmsContext ContextID,
                                     CMSREGISTER _cmsTRANSFORM* info,
                                     CMSREGISTER cmsUInt16Number wIn[],
                                     CMSREGISTER cmsUInt8Number* accum,
                                     CMSREGISTER cmsUInt32Number Stride)
{
   cmsUInt32Number nChan       = T_CHANNELS(info -> InputFormat);
   cmsUInt32Number SwapEndian  = T_ENDIAN16(info -> InputFormat);
   cmsUInt32Number DoSwap      = T_DOSWAP(info ->InputFormat);
   cmsUInt32Number Reverse     = T_FLAVOR(info ->InputFormat);
   cmsUInt32Number SwapFirst   = T_SWAPFIRST(info -> InputFormat);
   cmsUInt32Number ExtraFirst  = DoSwap ^ SwapFirst;
   cmsUInt32Number i;

   cmsUInt16Number alpha = (ExtraFirst ? accum[0] : accum[nChan - 1]);
   cmsUInt32Number alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(alpha));

    if (ExtraFirst) {
        accum += sizeof(cmsUInt16Number);
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt32Number v = *(cmsUInt16Number*) accum;

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        if (alpha_factor > 0) {

            v = (v << 16) / alpha_factor;
            if (v > 0xffff) v = 0xffff;
        }

        wIn[index] = (cmsUInt16Number) (Reverse ? REVERSE_FLAVOR_16(v) : v);

        accum += sizeof(cmsUInt16Number);
    }

    if (!ExtraFirst) {
        accum += sizeof(cmsUInt16Number);
    }

    return accum;

    cmsUNUSED_PARAMETER(Stride);
}



static
cmsUInt8Number* UnrollPlanarWords(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wIn[],
                                  CMSREGISTER cmsUInt8Number* accum,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap= T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse= T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapEndian = T_ENDIAN16(info -> InputFormat);
    cmsUInt32Number i;
    cmsUInt8Number* Init = accum;

    if (DoSwap) {
        accum += T_EXTRA(info -> InputFormat) * Stride;
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt16Number v = *(cmsUInt16Number*) accum;

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        wIn[index] = Reverse ? REVERSE_FLAVOR_16(v) : v;

        accum +=  Stride;
    }

    return (Init + sizeof(cmsUInt16Number));
}

static
cmsUInt8Number* UnrollPlanarWordsPremul(cmsContext ContextID,
                                        CMSREGISTER _cmsTRANSFORM* info,
                                        CMSREGISTER cmsUInt16Number wIn[],
                                        CMSREGISTER cmsUInt8Number* accum,
                                        CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap= T_DOSWAP(info ->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->InputFormat);
    cmsUInt32Number Reverse= T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapEndian = T_ENDIAN16(info -> InputFormat);
    cmsUInt32Number i;
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt8Number* Init = accum;

    cmsUInt16Number  alpha = (ExtraFirst ? accum[0] : accum[(nChan - 1) * Stride]);
    cmsUInt32Number alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(alpha));

    if (ExtraFirst) {
        accum += Stride;
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt32Number v = (cmsUInt32Number) *(cmsUInt16Number*) accum;

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        if (alpha_factor > 0) {

            v = (v << 16) / alpha_factor;
            if (v > 0xffff) v = 0xffff;
        }

        wIn[index] = (cmsUInt16Number) (Reverse ? REVERSE_FLAVOR_16(v) : v);

        accum +=  Stride;
    }

    return (Init + sizeof(cmsUInt16Number));
}

static
cmsUInt8Number* Unroll4Words(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2; // C
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2; // M
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2; // Y
    wIn[3] = *(cmsUInt16Number*) accum; accum+= 2; // K

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4WordsReverse(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = REVERSE_FLAVOR_16(*(cmsUInt16Number*) accum); accum+= 2; // C
    wIn[1] = REVERSE_FLAVOR_16(*(cmsUInt16Number*) accum); accum+= 2; // M
    wIn[2] = REVERSE_FLAVOR_16(*(cmsUInt16Number*) accum); accum+= 2; // Y
    wIn[3] = REVERSE_FLAVOR_16(*(cmsUInt16Number*) accum); accum+= 2; // K

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4WordsSwapFirst(cmsContext ContextID,
                                      CMSREGISTER _cmsTRANSFORM* info,
                                      CMSREGISTER cmsUInt16Number wIn[],
                                      CMSREGISTER cmsUInt8Number* accum,
                                      CMSREGISTER cmsUInt32Number Stride)
{
    wIn[3] = *(cmsUInt16Number*) accum; accum+= 2; // K
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2; // C
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2; // M
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2; // Y

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// KYMC
static
cmsUInt8Number* Unroll4WordsSwap(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[3] = *(cmsUInt16Number*) accum; accum+= 2; // K
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2; // Y
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2; // M
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2; // C

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll4WordsSwapSwapFirst(cmsContext ContextID,
                                          CMSREGISTER _cmsTRANSFORM* info,
                                          CMSREGISTER cmsUInt16Number wIn[],
                                          CMSREGISTER cmsUInt8Number* accum,
                                          CMSREGISTER cmsUInt32Number Stride)
{
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2; // K
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2; // Y
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2; // M
    wIn[3] = *(cmsUInt16Number*) accum; accum+= 2; // C

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3Words(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2;  // C R
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2;  // M G
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2;  // Y B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3WordsSwap(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[2] = *(cmsUInt16Number*) accum; accum+= 2;  // C R
    wIn[1] = *(cmsUInt16Number*) accum; accum+= 2;  // M G
    wIn[0] = *(cmsUInt16Number*) accum; accum+= 2;  // Y B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3WordsSkip1Swap(cmsContext ContextID,
                                      CMSREGISTER _cmsTRANSFORM* info,
                                      CMSREGISTER cmsUInt16Number wIn[],
                                      CMSREGISTER cmsUInt8Number* accum,
                                      CMSREGISTER cmsUInt32Number Stride)
{
    accum += 2; // A
    wIn[2] = *(cmsUInt16Number*) accum; accum += 2; // R
    wIn[1] = *(cmsUInt16Number*) accum; accum += 2; // G
    wIn[0] = *(cmsUInt16Number*) accum; accum += 2; // B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll3WordsSkip1SwapFirst(cmsContext ContextID,
                                           CMSREGISTER _cmsTRANSFORM* info,
                                           CMSREGISTER cmsUInt16Number wIn[],
                                           CMSREGISTER cmsUInt8Number* accum,
                                           CMSREGISTER cmsUInt32Number Stride)
{
    accum += 2; // A
    wIn[0] = *(cmsUInt16Number*) accum; accum += 2; // R
    wIn[1] = *(cmsUInt16Number*) accum; accum += 2; // G
    wIn[2] = *(cmsUInt16Number*) accum; accum += 2; // B

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll1Word(cmsContext ContextID,
                            CMSREGISTER _cmsTRANSFORM* info,
                            CMSREGISTER cmsUInt16Number wIn[],
                            CMSREGISTER cmsUInt8Number* accum,
                            CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = *(cmsUInt16Number*) accum; accum+= 2;   // L

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll1WordReversed(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = REVERSE_FLAVOR_16(*(cmsUInt16Number*) accum); accum+= 2;

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll1WordSkip3(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = wIn[1] = wIn[2] = *(cmsUInt16Number*) accum;

    accum += 8;

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Unroll2Words(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wIn[],
                             CMSREGISTER cmsUInt8Number* accum,
                             CMSREGISTER cmsUInt32Number Stride)
{
    wIn[0] = *(cmsUInt16Number*) accum; accum += 2;    // ch1
    wIn[1] = *(cmsUInt16Number*) accum; accum += 2;    // ch2

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


// This is a conversion of Lab double to 16 bits
static
cmsUInt8Number* UnrollLabDoubleTo16(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number  Stride)
{
    if (T_PLANAR(info -> InputFormat)) {

        cmsCIELab Lab;
        cmsUInt8Number* pos_L;
        cmsUInt8Number* pos_a;
        cmsUInt8Number* pos_b;

        pos_L = accum;
        pos_a = accum + Stride;
        pos_b = accum + Stride * 2;

        Lab.L = *(cmsFloat64Number*) pos_L;
        Lab.a = *(cmsFloat64Number*) pos_a;
        Lab.b = *(cmsFloat64Number*) pos_b;

        cmsFloat2LabEncoded(ContextID, wIn, &Lab);
        return accum + sizeof(cmsFloat64Number);
    }
    else {

        cmsFloat2LabEncoded(ContextID, wIn, (cmsCIELab*) accum);
        accum += sizeof(cmsCIELab) + T_EXTRA(info ->InputFormat) * sizeof(cmsFloat64Number);
        return accum;
    }
}


// This is a conversion of Lab float to 16 bits
static
cmsUInt8Number* UnrollLabFloatTo16(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wIn[],
                                   CMSREGISTER cmsUInt8Number* accum,
                                   CMSREGISTER cmsUInt32Number  Stride)
{
    cmsCIELab Lab;

    if (T_PLANAR(info -> InputFormat)) {

        cmsUInt8Number* pos_L;
        cmsUInt8Number* pos_a;
        cmsUInt8Number* pos_b;

        pos_L = accum;
        pos_a = accum + Stride;
        pos_b = accum + Stride * 2;

        Lab.L = *(cmsFloat32Number*)pos_L;
        Lab.a = *(cmsFloat32Number*)pos_a;
        Lab.b = *(cmsFloat32Number*)pos_b;

        cmsFloat2LabEncoded(ContextID, wIn, &Lab);
        return accum + sizeof(cmsFloat32Number);
    }
    else {

        Lab.L = ((cmsFloat32Number*) accum)[0];
        Lab.a = ((cmsFloat32Number*) accum)[1];
        Lab.b = ((cmsFloat32Number*) accum)[2];

        cmsFloat2LabEncoded(ContextID, wIn, &Lab);
        accum += (3 + T_EXTRA(info ->InputFormat)) * sizeof(cmsFloat32Number);
        return accum;
    }
}

// This is a conversion of XYZ double to 16 bits
static
cmsUInt8Number* UnrollXYZDoubleTo16(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wIn[],
                                    CMSREGISTER cmsUInt8Number* accum,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    if (T_PLANAR(info -> InputFormat)) {

        cmsCIEXYZ XYZ;
        cmsUInt8Number* pos_X;
        cmsUInt8Number* pos_Y;
        cmsUInt8Number* pos_Z;

        pos_X = accum;
        pos_Y = accum + Stride;
        pos_Z = accum + Stride * 2;

        XYZ.X = *(cmsFloat64Number*)pos_X;
        XYZ.Y = *(cmsFloat64Number*)pos_Y;
        XYZ.Z = *(cmsFloat64Number*)pos_Z;

        cmsFloat2XYZEncoded(ContextID, wIn, &XYZ);

        return accum + sizeof(cmsFloat64Number);

    }

    else {
        cmsFloat2XYZEncoded(ContextID, wIn, (cmsCIEXYZ*) accum);
        accum += sizeof(cmsCIEXYZ) + T_EXTRA(info ->InputFormat) * sizeof(cmsFloat64Number);

        return accum;
    }
}

// This is a conversion of XYZ float to 16 bits
static
cmsUInt8Number* UnrollXYZFloatTo16(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wIn[],
                                   CMSREGISTER cmsUInt8Number* accum,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    if (T_PLANAR(info -> InputFormat)) {

        cmsCIEXYZ XYZ;
        cmsUInt8Number* pos_X;
        cmsUInt8Number* pos_Y;
        cmsUInt8Number* pos_Z;

        pos_X = accum;
        pos_Y = accum + Stride;
        pos_Z = accum + Stride * 2;

        XYZ.X = *(cmsFloat32Number*)pos_X;
        XYZ.Y = *(cmsFloat32Number*)pos_Y;
        XYZ.Z = *(cmsFloat32Number*)pos_Z;

        cmsFloat2XYZEncoded(ContextID, wIn, &XYZ);

        return accum + sizeof(cmsFloat32Number);

    }

    else {
        cmsFloat32Number* Pt = (cmsFloat32Number*) accum;
        cmsCIEXYZ XYZ;

        XYZ.X = Pt[0];
        XYZ.Y = Pt[1];
        XYZ.Z = Pt[2];
        cmsFloat2XYZEncoded(ContextID, wIn, &XYZ);

        accum += 3 * sizeof(cmsFloat32Number) + T_EXTRA(info ->InputFormat) * sizeof(cmsFloat32Number);

        return accum;
    }
}

// Check if space is marked as ink
cmsINLINE cmsBool IsInkSpace(cmsUInt32Number Type)
{
    switch (T_COLORSPACE(Type)) {

     case PT_CMY:
     case PT_CMYK:
     case PT_MCH5:
     case PT_MCH6:
     case PT_MCH7:
     case PT_MCH8:
     case PT_MCH9:
     case PT_MCH10:
     case PT_MCH11:
     case PT_MCH12:
     case PT_MCH13:
     case PT_MCH14:
     case PT_MCH15: return TRUE;

     default: return FALSE;
    }
}

// Return the size in bytes of a given formatter
static
cmsUInt32Number PixelSize(cmsUInt32Number Format)
{
    cmsUInt32Number fmt_bytes = T_BYTES(Format);

    // For double, the T_BYTES field is zero
    if (fmt_bytes == 0)
        return sizeof(cmsUInt64Number);

    // Otherwise, it is already correct for all formats
    return fmt_bytes;
}

// Inks does come in percentage, remaining cases are between 0..1.0, again to 16 bits
static
cmsUInt8Number* UnrollDoubleTo16(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wIn[],
                                 CMSREGISTER cmsUInt8Number* accum,
                                 CMSREGISTER cmsUInt32Number Stride)
{

    cmsUInt32Number nChan      = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap     = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> InputFormat);
    cmsUInt32Number Extra      = T_EXTRA(info -> InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar     = T_PLANAR(info -> InputFormat);
    cmsFloat64Number v;
    cmsUInt16Number  vi;
    cmsUInt32Number i, start = 0;
    cmsFloat64Number maximum = IsInkSpace(info ->InputFormat) ? 655.35 : 65535.0;


    Stride /= PixelSize(info->InputFormat);

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = (cmsFloat32Number) ((cmsFloat64Number*) accum)[(i + start) * Stride];
        else
            v = (cmsFloat32Number) ((cmsFloat64Number*) accum)[i + start];

        vi = _cmsQuickSaturateWord(v * maximum);

        if (Reverse)
            vi = REVERSE_FLAVOR_16(vi);

        wIn[index] = vi;
    }


    if (Extra == 0 && SwapFirst) {
        cmsUInt16Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsUInt16Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsFloat64Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsFloat64Number);
}



static
cmsUInt8Number* UnrollFloatTo16(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wIn[],
                                CMSREGISTER cmsUInt8Number* accum,
                                CMSREGISTER cmsUInt32Number Stride)
{

    cmsUInt32Number nChan  = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap   = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> InputFormat);
    cmsUInt32Number Extra   = T_EXTRA(info -> InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar     = T_PLANAR(info -> InputFormat);
    cmsFloat32Number v;
    cmsUInt16Number  vi;
    cmsUInt32Number i, start = 0;
    cmsFloat64Number maximum = IsInkSpace(info ->InputFormat) ? 655.35 : 65535.0;

    Stride /= PixelSize(info->InputFormat);

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = (cmsFloat32Number) ((cmsFloat32Number*) accum)[(i + start) * Stride];
        else
            v = (cmsFloat32Number) ((cmsFloat32Number*) accum)[i + start];

        vi = _cmsQuickSaturateWord(v * maximum);

        if (Reverse)
            vi = REVERSE_FLAVOR_16(vi);

        wIn[index] = vi;
    }


    if (Extra == 0 && SwapFirst) {
        cmsUInt16Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsUInt16Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsFloat32Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsFloat32Number);
}




// For 1 channel, we need to duplicate data (it comes in 0..1.0 range)
static
cmsUInt8Number* UnrollDouble1Chan(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wIn[],
                                  CMSREGISTER cmsUInt8Number* accum,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    cmsFloat64Number* Inks = (cmsFloat64Number*) accum;

    wIn[0] = wIn[1] = wIn[2] = _cmsQuickSaturateWord(Inks[0] * 65535.0);

    return accum + sizeof(cmsFloat64Number);

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

//-------------------------------------------------------------------------------------------------------------------

// For anything going from cmsUInt8Number
static
cmsUInt8Number* Unroll8ToFloat(cmsContext ContextID,
                               _cmsTRANSFORM* info,
                               cmsFloat32Number wIn[],
                               cmsUInt8Number* accum,
                               cmsUInt32Number Stride)
{

    cmsUInt32Number nChan = T_CHANNELS(info->InputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->InputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->InputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar = T_PLANAR(info->InputFormat);
    cmsFloat32Number v;
    cmsUInt32Number i, start = 0;

    Stride /= PixelSize(info->InputFormat);

    if (ExtraFirst)
        start = Extra;

    for (i = 0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = (cmsFloat32Number) ((cmsUInt8Number *)accum)[(i + start) * Stride];
        else
            v = (cmsFloat32Number) ((cmsUInt8Number *)accum)[i + start];

        v /= 255.0F;

        wIn[index] = Reverse ? 1 - v : v;
    }


    if (Extra == 0 && SwapFirst) {
        cmsFloat32Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan - 1) * sizeof(cmsFloat32Number));
        wIn[nChan - 1] = tmp;
    }

    if (T_PLANAR(info->InputFormat))
        return accum + sizeof(cmsUInt8Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsUInt8Number);
}


// For anything going from cmsUInt16Number
static
cmsUInt8Number* Unroll16ToFloat(cmsContext ContextID,
                                _cmsTRANSFORM* info,
                                cmsFloat32Number wIn[],
                                cmsUInt8Number* accum,
                                cmsUInt32Number Stride)
{

    cmsUInt32Number nChan = T_CHANNELS(info->InputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->InputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->InputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar = T_PLANAR(info->InputFormat);
    cmsFloat32Number v;
    cmsUInt32Number i, start = 0;

    Stride /= PixelSize(info->InputFormat);

    if (ExtraFirst)
        start = Extra;

    for (i = 0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = (cmsFloat32Number)((cmsUInt16Number*)accum)[(i + start) * Stride];
        else
            v = (cmsFloat32Number)((cmsUInt16Number*)accum)[i + start];

        v /= 65535.0F;

        wIn[index] = Reverse ? 1 - v : v;
    }


    if (Extra == 0 && SwapFirst) {
        cmsFloat32Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan - 1) * sizeof(cmsFloat32Number));
        wIn[nChan - 1] = tmp;
    }

    if (T_PLANAR(info->InputFormat))
        return accum + sizeof(cmsUInt16Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsUInt16Number);
}


// For anything going from cmsFloat32Number
static
cmsUInt8Number* UnrollFloatsToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                    cmsFloat32Number wIn[],
                                    cmsUInt8Number* accum,
                                    cmsUInt32Number Stride)
{

    cmsUInt32Number nChan = T_CHANNELS(info->InputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->InputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->InputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar = T_PLANAR(info->InputFormat);
    cmsUInt32Number Premul = T_PREMUL(info->InputFormat);
    cmsFloat32Number v;
    cmsUInt32Number i, start = 0;
    cmsFloat32Number maximum = IsInkSpace(info->InputFormat) ? 100.0F : 1.0F;
    cmsFloat32Number alpha_factor = 1.0f;
    cmsFloat32Number* ptr = (cmsFloat32Number*)accum;

    Stride /= PixelSize(info->InputFormat);

    if (Premul && Extra)
    {
        if (Planar)
            alpha_factor = (ExtraFirst ? ptr[0] : ptr[nChan * Stride]) / maximum;
        else
            alpha_factor = (ExtraFirst ? ptr[0] : ptr[nChan]) / maximum;
    }

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = ptr[(i + start) * Stride];
        else
            v = ptr[i + start];

        if (Premul && alpha_factor > 0)
            v /= alpha_factor;

        v /= maximum;

        wIn[index] = Reverse ? 1 - v : v;
    }


    if (Extra == 0 && SwapFirst) {
        cmsFloat32Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsFloat32Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsFloat32Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsFloat32Number);
}

// For anything going from double

static
cmsUInt8Number* UnrollDoublesToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                    cmsFloat32Number wIn[],
                                    cmsUInt8Number* accum,
                                    cmsUInt32Number Stride)
{

    cmsUInt32Number nChan = T_CHANNELS(info->InputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->InputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->InputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->InputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar = T_PLANAR(info->InputFormat);
    cmsUInt32Number Premul = T_PREMUL(info->InputFormat);
    cmsFloat64Number v;
    cmsUInt32Number i, start = 0;
    cmsFloat64Number maximum = IsInkSpace(info ->InputFormat) ? 100.0 : 1.0;
    cmsFloat64Number alpha_factor = 1.0;
    cmsFloat64Number* ptr = (cmsFloat64Number*)accum;

    Stride /= PixelSize(info->InputFormat);

    if (Premul && Extra)
    {
        if (Planar)
            alpha_factor = (ExtraFirst ? ptr[0] : ptr[(nChan) * Stride]) / maximum;
        else
            alpha_factor = (ExtraFirst ? ptr[0] : ptr[nChan]) / maximum;
    }

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = (cmsFloat64Number) ((cmsFloat64Number*) accum)[(i + start)  * Stride];
        else
            v = (cmsFloat64Number) ((cmsFloat64Number*) accum)[i + start];


        if (Premul && alpha_factor > 0)
            v /= alpha_factor;

        v /= maximum;

        wIn[index] = (cmsFloat32Number) (Reverse ? 1.0 - v : v);
    }


    if (Extra == 0 && SwapFirst) {
        cmsFloat32Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsFloat32Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsFloat64Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsFloat64Number);
}



// From Lab double to cmsFloat32Number
static
cmsUInt8Number* UnrollLabDoubleToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                       cmsFloat32Number wIn[],
                                       cmsUInt8Number* accum,
                                       cmsUInt32Number Stride)
{
    cmsFloat64Number* Pt = (cmsFloat64Number*) accum;

    if (T_PLANAR(info -> InputFormat)) {

        Stride /= PixelSize(info->InputFormat);

        wIn[0] = (cmsFloat32Number) (Pt[0] / 100.0);                 // from 0..100 to 0..1
        wIn[1] = (cmsFloat32Number) ((Pt[Stride] + 128) / 255.0);    // form -128..+127 to 0..1
        wIn[2] = (cmsFloat32Number) ((Pt[Stride*2] + 128) / 255.0);

        return accum + sizeof(cmsFloat64Number);
    }
    else {

        wIn[0] = (cmsFloat32Number) (Pt[0] / 100.0);            // from 0..100 to 0..1
        wIn[1] = (cmsFloat32Number) ((Pt[1] + 128) / 255.0);    // form -128..+127 to 0..1
        wIn[2] = (cmsFloat32Number) ((Pt[2] + 128) / 255.0);

        accum += sizeof(cmsFloat64Number)*(3 + T_EXTRA(info ->InputFormat));
        return accum;
    }
}

// From Lab double to cmsFloat32Number
static
cmsUInt8Number* UnrollLabFloatToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                      cmsFloat32Number wIn[],
                                      cmsUInt8Number* accum,
                                      cmsUInt32Number Stride)
{
    cmsFloat32Number* Pt = (cmsFloat32Number*) accum;

    if (T_PLANAR(info -> InputFormat)) {

        Stride /= PixelSize(info->InputFormat);

        wIn[0] = (cmsFloat32Number) (Pt[0] / 100.0);                 // from 0..100 to 0..1
        wIn[1] = (cmsFloat32Number) ((Pt[Stride] + 128) / 255.0);    // form -128..+127 to 0..1
        wIn[2] = (cmsFloat32Number) ((Pt[Stride*2] + 128) / 255.0);

        return accum + sizeof(cmsFloat32Number);
    }
    else {

        wIn[0] = (cmsFloat32Number) (Pt[0] / 100.0);            // from 0..100 to 0..1
        wIn[1] = (cmsFloat32Number) ((Pt[1] + 128) / 255.0);    // form -128..+127 to 0..1
        wIn[2] = (cmsFloat32Number) ((Pt[2] + 128) / 255.0);

        accum += sizeof(cmsFloat32Number)*(3 + T_EXTRA(info ->InputFormat));
        return accum;
    }
}

// 1.15 fixed point, that means maximum value is MAX_ENCODEABLE_XYZ (0xFFFF)
static
cmsUInt8Number* UnrollXYZDoubleToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                       cmsFloat32Number wIn[],
                                       cmsUInt8Number* accum,
                                       cmsUInt32Number Stride)
{
    cmsFloat64Number* Pt = (cmsFloat64Number*) accum;

    if (T_PLANAR(info -> InputFormat)) {

        Stride /= PixelSize(info->InputFormat);

        wIn[0] = (cmsFloat32Number) (Pt[0] / MAX_ENCODEABLE_XYZ);
        wIn[1] = (cmsFloat32Number) (Pt[Stride] / MAX_ENCODEABLE_XYZ);
        wIn[2] = (cmsFloat32Number) (Pt[Stride*2] / MAX_ENCODEABLE_XYZ);

        return accum + sizeof(cmsFloat64Number);
    }
    else {

        wIn[0] = (cmsFloat32Number) (Pt[0] / MAX_ENCODEABLE_XYZ);
        wIn[1] = (cmsFloat32Number) (Pt[1] / MAX_ENCODEABLE_XYZ);
        wIn[2] = (cmsFloat32Number) (Pt[2] / MAX_ENCODEABLE_XYZ);

        accum += sizeof(cmsFloat64Number)*(3 + T_EXTRA(info ->InputFormat));
        return accum;
    }
}

static
cmsUInt8Number* UnrollXYZFloatToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                      cmsFloat32Number wIn[],
                                      cmsUInt8Number* accum,
                                      cmsUInt32Number Stride)
{
    cmsFloat32Number* Pt = (cmsFloat32Number*) accum;

    if (T_PLANAR(info -> InputFormat)) {

        Stride /= PixelSize(info->InputFormat);

        wIn[0] = (cmsFloat32Number) (Pt[0] / MAX_ENCODEABLE_XYZ);
        wIn[1] = (cmsFloat32Number) (Pt[Stride] / MAX_ENCODEABLE_XYZ);
        wIn[2] = (cmsFloat32Number) (Pt[Stride*2] / MAX_ENCODEABLE_XYZ);

        return accum + sizeof(cmsFloat32Number);
    }
    else {

        wIn[0] = (cmsFloat32Number) (Pt[0] / MAX_ENCODEABLE_XYZ);
        wIn[1] = (cmsFloat32Number) (Pt[1] / MAX_ENCODEABLE_XYZ);
        wIn[2] = (cmsFloat32Number) (Pt[2] / MAX_ENCODEABLE_XYZ);

        accum += sizeof(cmsFloat32Number)*(3 + T_EXTRA(info ->InputFormat));
        return accum;
    }
}


cmsINLINE void lab4toFloat(cmsFloat32Number wIn[], cmsUInt16Number lab4[3])
{
    cmsFloat32Number L = (cmsFloat32Number) lab4[0] / 655.35F;
    cmsFloat32Number a = ((cmsFloat32Number) lab4[1] / 257.0F) - 128.0F;
    cmsFloat32Number b = ((cmsFloat32Number) lab4[2] / 257.0F) - 128.0F;

    wIn[0] = (L / 100.0F);                    // from 0..100 to 0..1
    wIn[1] = ((a + 128.0F) / 255.0F);         // form -128..+127 to 0..1
    wIn[2] = ((b + 128.0F) / 255.0F);

}

static
cmsUInt8Number* UnrollLabV2_8ToFloat(cmsContext ContextID,
                                      _cmsTRANSFORM* info,
                                      cmsFloat32Number wIn[],
                                      cmsUInt8Number* accum,
                                      cmsUInt32Number Stride)
{
    cmsUInt16Number lab4[3];

    lab4[0] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // L
    lab4[1] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // a
    lab4[2] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // b

    lab4toFloat(wIn, lab4);

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* UnrollALabV2_8ToFloat(cmsContext ContextID,
                                      _cmsTRANSFORM* info,
                                      cmsFloat32Number wIn[],
                                      cmsUInt8Number* accum,
                                      cmsUInt32Number Stride)
{
    cmsUInt16Number lab4[3];

    accum++;  // A
    lab4[0] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // L
    lab4[1] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // a
    lab4[2] = FomLabV2ToLabV4(FROM_8_TO_16(*accum)); accum++;     // b

    lab4toFloat(wIn, lab4);

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* UnrollLabV2_16ToFloat(cmsContext ContextID,
                                      _cmsTRANSFORM* info,
                                      cmsFloat32Number wIn[],
                                      cmsUInt8Number* accum,
                                      cmsUInt32Number Stride)
{
    cmsUInt16Number lab4[3];

    lab4[0] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // L
    lab4[1] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // a
    lab4[2] = FomLabV2ToLabV4(*(cmsUInt16Number*) accum); accum += 2;     // b

    lab4toFloat(wIn, lab4);

    return accum;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


// Packing routines -----------------------------------------------------------------------------------------------------------


// Generic chunky for byte
static
cmsUInt8Number* PackChunkyBytes(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wOut[],
                                CMSREGISTER cmsUInt8Number* output,
                                CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Premul = T_PREMUL(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt8Number* swap1;
    cmsUInt16Number v = 0;
    cmsUInt32Number i;
    cmsUInt32Number alpha_factor = 0;

    swap1 = output;

    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(output[0]));

        output += Extra;
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(output[nChan]));
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = wOut[index];

        if (Reverse)
            v = REVERSE_FLAVOR_16(v);

        if (Premul)
        {
            v = (cmsUInt16Number)((cmsUInt32Number)((cmsUInt32Number)v * alpha_factor + 0x8000) >> 16);
        }

        *output++ = FROM_16_TO_8(v);
    }

    if (!ExtraFirst) {
        output += Extra;
    }

    if (Extra == 0 && SwapFirst) {

        memmove(swap1 + 1, swap1, nChan-1);
        *swap1 = FROM_16_TO_8(v);
    }

    return output;

    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* PackChunkyWords(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wOut[],
                                CMSREGISTER cmsUInt8Number* output,
                                CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number SwapEndian = T_ENDIAN16(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Premul = T_PREMUL(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt16Number* swap1;
    cmsUInt16Number v = 0;
    cmsUInt32Number i;
    cmsUInt32Number alpha_factor = 0;

    swap1 = (cmsUInt16Number*) output;

    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(*(cmsUInt16Number*) output);

        output += Extra * sizeof(cmsUInt16Number);
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(((cmsUInt16Number*) output)[nChan]);
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = wOut[index];

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        if (Reverse)
            v = REVERSE_FLAVOR_16(v);

        if (Premul)
        {
            v = (cmsUInt16Number)((cmsUInt32Number)((cmsUInt32Number)v * alpha_factor + 0x8000) >> 16);
        }

        *(cmsUInt16Number*) output = v;

        output += sizeof(cmsUInt16Number);
    }

    if (!ExtraFirst) {
        output += Extra * sizeof(cmsUInt16Number);
    }

    if (Extra == 0 && SwapFirst) {

        memmove(swap1 + 1, swap1, (nChan-1)* sizeof(cmsUInt16Number));
        *swap1 = v;
    }

    return output;

    cmsUNUSED_PARAMETER(Stride);
}



static
cmsUInt8Number* PackPlanarBytes(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wOut[],
                                CMSREGISTER cmsUInt8Number* output,
                                CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Premul = T_PREMUL(info->OutputFormat);
    cmsUInt32Number i;
    cmsUInt8Number* Init = output;
    cmsUInt32Number alpha_factor = 0;


    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(output[0]));

        output += Extra * Stride;
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(FROM_8_TO_16(output[nChan * Stride]));
    }


    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;
        cmsUInt16Number v = wOut[index];

        if (Reverse)
            v = REVERSE_FLAVOR_16(v);

        if (Premul)
        {
            v = (cmsUInt16Number)((cmsUInt32Number)((cmsUInt32Number)v * alpha_factor + 0x8000) >> 16);
        }

        *(cmsUInt8Number*)output = FROM_16_TO_8(v);

        output += Stride;
    }

    return (Init + 1);

    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* PackPlanarWords(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wOut[],
                                CMSREGISTER cmsUInt8Number* output,
                                CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Premul = T_PREMUL(info->OutputFormat);
    cmsUInt32Number SwapEndian = T_ENDIAN16(info->OutputFormat);
    cmsUInt32Number i;
    cmsUInt8Number* Init = output;
    cmsUInt16Number v;
    cmsUInt32Number alpha_factor = 0;

    if (ExtraFirst) {

        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(((cmsUInt16Number*) output)[0]);

        output += Extra * Stride;
    }
    else
    {
        if (Premul && Extra)
            alpha_factor = _cmsToFixedDomain(((cmsUInt16Number*)output)[nChan * Stride]);
    }

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = wOut[index];

        if (SwapEndian)
            v = CHANGE_ENDIAN(v);

        if (Reverse)
            v =  REVERSE_FLAVOR_16(v);

        if (Premul)
        {
            v = (cmsUInt16Number)((cmsUInt32Number)((cmsUInt32Number)v * alpha_factor + 0x8000) >> 16);
        }

        *(cmsUInt16Number*) output = v;
        output += Stride;
    }

    return (Init + sizeof(cmsUInt16Number));
}

// CMYKcm (unrolled for speed)

static
cmsUInt8Number* Pack6Bytes(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[3]);
    *output++ = FROM_16_TO_8(wOut[4]);
    *output++ = FROM_16_TO_8(wOut[5]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// KCMYcm

static
cmsUInt8Number* Pack6BytesSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[5]);
    *output++ = FROM_16_TO_8(wOut[4]);
    *output++ = FROM_16_TO_8(wOut[3]);
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// CMYKcm
static
cmsUInt8Number* Pack6Words(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[3];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[4];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[5];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// KCMYcm
static
cmsUInt8Number* Pack6WordsSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[5];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[4];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[3];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack4Bytes(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[3]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack4BytesReverse(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wOut[],
                                  CMSREGISTER cmsUInt8Number* output,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = REVERSE_FLAVOR_8(FROM_16_TO_8(wOut[0]));
    *output++ = REVERSE_FLAVOR_8(FROM_16_TO_8(wOut[1]));
    *output++ = REVERSE_FLAVOR_8(FROM_16_TO_8(wOut[2]));
    *output++ = REVERSE_FLAVOR_8(FROM_16_TO_8(wOut[3]));

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack4BytesSwapFirst(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[3]);
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// ABGR
static
cmsUInt8Number* Pack4BytesSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[3]);
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack4BytesSwapSwapFirst(cmsContext ContextID,
                                        CMSREGISTER _cmsTRANSFORM* info,
                                        CMSREGISTER cmsUInt16Number wOut[],
                                        CMSREGISTER cmsUInt8Number* output,
                                        CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[3]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack4Words(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[3];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack4WordsReverse(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wOut[],
                                  CMSREGISTER cmsUInt8Number* output,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = REVERSE_FLAVOR_16(wOut[0]);
    output+= 2;
    *(cmsUInt16Number*) output = REVERSE_FLAVOR_16(wOut[1]);
    output+= 2;
    *(cmsUInt16Number*) output = REVERSE_FLAVOR_16(wOut[2]);
    output+= 2;
    *(cmsUInt16Number*) output = REVERSE_FLAVOR_16(wOut[3]);
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// ABGR
static
cmsUInt8Number* Pack4WordsSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[3];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

// CMYK
static
cmsUInt8Number* Pack4WordsBigEndian(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[0]);
    output+= 2;
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[1]);
    output+= 2;
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[2]);
    output+= 2;
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[3]);
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* PackLabV2_8(cmsContext ContextID,
                            CMSREGISTER _cmsTRANSFORM* info,
                            CMSREGISTER cmsUInt16Number wOut[],
                            CMSREGISTER cmsUInt8Number* output,
                            CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[0]));
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[1]));
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[2]));

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* PackALabV2_8(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wOut[],
                             CMSREGISTER cmsUInt8Number* output,
                             CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[0]));
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[1]));
    *output++ = FROM_16_TO_8(FomLabV4ToLabV2(wOut[2]));

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* PackLabV2_16(cmsContext ContextID,
                             CMSREGISTER _cmsTRANSFORM* info,
                             CMSREGISTER cmsUInt16Number wOut[],
                             CMSREGISTER cmsUInt8Number* output,
                             CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = FomLabV4ToLabV2(wOut[0]);
    output += 2;
    *(cmsUInt16Number*) output = FomLabV4ToLabV2(wOut[1]);
    output += 2;
    *(cmsUInt16Number*) output = FomLabV4ToLabV2(wOut[2]);
    output += 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3Bytes(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesOptimized(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = (wOut[0] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[2] & 0xFFU);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesSwapOptimized(cmsContext ContextID,
                                        CMSREGISTER _cmsTRANSFORM* info,
                                        CMSREGISTER cmsUInt16Number wOut[],
                                        CMSREGISTER cmsUInt8Number* output,
                                        CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = (wOut[2] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[0] & 0xFFU);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack3Words(cmsContext ContextID,
                           CMSREGISTER _cmsTRANSFORM* info,
                           CMSREGISTER cmsUInt16Number wOut[],
                           CMSREGISTER cmsUInt8Number* output,
                           CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3WordsSwap(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3WordsBigEndian(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[0]);
    output+= 2;
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[1]);
    output+= 2;
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[2]);
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wOut[],
                                   CMSREGISTER cmsUInt8Number* output,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);
    output++;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1Optimized(cmsContext ContextID,
                                            CMSREGISTER _cmsTRANSFORM* info,
                                            CMSREGISTER cmsUInt16Number wOut[],
                                            CMSREGISTER cmsUInt8Number* output,
                                            CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = (wOut[0] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[2] & 0xFFU);
    output++;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack3BytesAndSkip1SwapFirst(cmsContext ContextID,
                                            CMSREGISTER _cmsTRANSFORM* info,
                                            CMSREGISTER cmsUInt16Number wOut[],
                                            CMSREGISTER cmsUInt8Number* output,
                                            CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = FROM_16_TO_8(wOut[0]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[2]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1SwapFirstOptimized(cmsContext ContextID,
                                                     CMSREGISTER _cmsTRANSFORM* info,
                                                     CMSREGISTER cmsUInt16Number wOut[],
                                                     CMSREGISTER cmsUInt8Number* output,
                                                     CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = (wOut[0] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[2] & 0xFFU);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1Swap(cmsContext ContextID,
                                       CMSREGISTER _cmsTRANSFORM* info,
                                       CMSREGISTER cmsUInt16Number wOut[],
                                       CMSREGISTER cmsUInt8Number* output,
                                       CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1SwapOptimized(cmsContext ContextID,
                                                CMSREGISTER _cmsTRANSFORM* info,
                                                CMSREGISTER cmsUInt16Number wOut[],
                                                CMSREGISTER cmsUInt8Number* output,
                                                CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = (wOut[2] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[0] & 0xFFU);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack3BytesAndSkip1SwapSwapFirst(cmsContext ContextID,
                                                CMSREGISTER _cmsTRANSFORM* info,
                                                CMSREGISTER cmsUInt16Number wOut[],
                                                CMSREGISTER cmsUInt8Number* output,
                                                CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[2]);
    *output++ = FROM_16_TO_8(wOut[1]);
    *output++ = FROM_16_TO_8(wOut[0]);
    output++;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3BytesAndSkip1SwapSwapFirstOptimized(cmsContext ContextID,
                                                         CMSREGISTER _cmsTRANSFORM* info,
                                                         CMSREGISTER cmsUInt16Number wOut[],
                                                         CMSREGISTER cmsUInt8Number* output,
                                                         CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = (wOut[2] & 0xFFU);
    *output++ = (wOut[1] & 0xFFU);
    *output++ = (wOut[0] & 0xFFU);
    output++;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3WordsAndSkip1(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wOut[],
                                   CMSREGISTER cmsUInt8Number* output,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack3WordsAndSkip1Swap(cmsContext ContextID,
                                       CMSREGISTER _cmsTRANSFORM* info,
                                       CMSREGISTER cmsUInt16Number wOut[],
                                       CMSREGISTER cmsUInt8Number* output,
                                       CMSREGISTER cmsUInt32Number Stride)
{
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack3WordsAndSkip1SwapFirst(cmsContext ContextID,
                                            CMSREGISTER _cmsTRANSFORM* info,
                                            CMSREGISTER cmsUInt16Number wOut[],
                                            CMSREGISTER cmsUInt8Number* output,
                                            CMSREGISTER cmsUInt32Number Stride)
{
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack3WordsAndSkip1SwapSwapFirst(cmsContext ContextID,
                                                CMSREGISTER _cmsTRANSFORM* info,
                                                CMSREGISTER cmsUInt16Number wOut[],
                                                CMSREGISTER cmsUInt8Number* output,
                                                CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[2];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[1];
    output+= 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}



static
cmsUInt8Number* Pack1Byte(cmsContext ContextID,
                          CMSREGISTER _cmsTRANSFORM* info,
                          CMSREGISTER cmsUInt16Number wOut[],
                          CMSREGISTER cmsUInt8Number* output,
                          CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack1ByteReversed(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wOut[],
                                  CMSREGISTER cmsUInt8Number* output,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(REVERSE_FLAVOR_16(wOut[0]));

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack1ByteSkip1(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *output++ = FROM_16_TO_8(wOut[0]);
    output++;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack1ByteSkip1SwapFirst(cmsContext ContextID,
                                        CMSREGISTER _cmsTRANSFORM* info,
                                        CMSREGISTER cmsUInt16Number wOut[],
                                        CMSREGISTER cmsUInt8Number* output,
                                        CMSREGISTER cmsUInt32Number Stride)
{
    output++;
    *output++ = FROM_16_TO_8(wOut[0]);

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack1Word(cmsContext ContextID,
                          CMSREGISTER _cmsTRANSFORM* info,
                          CMSREGISTER cmsUInt16Number wOut[],
                          CMSREGISTER cmsUInt8Number* output,
                          CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack1WordReversed(cmsContext ContextID,
                                  CMSREGISTER _cmsTRANSFORM* info,
                                  CMSREGISTER cmsUInt16Number wOut[],
                                  CMSREGISTER cmsUInt8Number* output,
                                  CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = REVERSE_FLAVOR_16(wOut[0]);
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack1WordBigEndian(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wOut[],
                                   CMSREGISTER cmsUInt8Number* output,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = CHANGE_ENDIAN(wOut[0]);
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


static
cmsUInt8Number* Pack1WordSkip1(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
    *(cmsUInt16Number*) output = wOut[0];
    output+= 4;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}

static
cmsUInt8Number* Pack1WordSkip1SwapFirst(cmsContext ContextID,
                                        CMSREGISTER _cmsTRANSFORM* info,
                                        CMSREGISTER cmsUInt16Number wOut[],
                                        CMSREGISTER cmsUInt8Number* output,
                                        CMSREGISTER cmsUInt32Number Stride)
{
    output += 2;
    *(cmsUInt16Number*) output = wOut[0];
    output+= 2;

    return output;

    cmsUNUSED_PARAMETER(info);
    cmsUNUSED_PARAMETER(Stride);
}


// Unencoded Float values -- don't try optimize speed
static
cmsUInt8Number* PackLabDoubleFrom16(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{

    if (T_PLANAR(info -> OutputFormat)) {

        cmsCIELab  Lab;
        cmsFloat64Number* Out = (cmsFloat64Number*) output;
        cmsLabEncoded2Float(ContextID, &Lab, wOut);

        Out[0]        = Lab.L;
        Out[Stride]   = Lab.a;
        Out[Stride*2] = Lab.b;

        return output + sizeof(cmsFloat64Number);
    }
    else {

        cmsLabEncoded2Float(ContextID, (cmsCIELab*) output, wOut);
        return output + (sizeof(cmsCIELab) + T_EXTRA(info ->OutputFormat) * sizeof(cmsFloat64Number));
    }
}


static
cmsUInt8Number* PackLabFloatFrom16(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* info,
                                   CMSREGISTER cmsUInt16Number wOut[],
                                   CMSREGISTER cmsUInt8Number* output,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    cmsCIELab  Lab;
    cmsLabEncoded2Float(ContextID, &Lab, wOut);

    if (T_PLANAR(info -> OutputFormat)) {

        cmsFloat32Number* Out = (cmsFloat32Number*) output;

        Stride /= PixelSize(info->OutputFormat);

        Out[0]        = (cmsFloat32Number)Lab.L;
        Out[Stride]   = (cmsFloat32Number)Lab.a;
        Out[Stride*2] = (cmsFloat32Number)Lab.b;

        return output + sizeof(cmsFloat32Number);
    }
    else {

       ((cmsFloat32Number*) output)[0] = (cmsFloat32Number) Lab.L;
       ((cmsFloat32Number*) output)[1] = (cmsFloat32Number) Lab.a;
       ((cmsFloat32Number*) output)[2] = (cmsFloat32Number) Lab.b;

        return output + (3 + T_EXTRA(info ->OutputFormat)) * sizeof(cmsFloat32Number);
    }
}

static
cmsUInt8Number* PackXYZDoubleFrom16(cmsContext ContextID,
                                    CMSREGISTER _cmsTRANSFORM* Info,
                                    CMSREGISTER cmsUInt16Number wOut[],
                                    CMSREGISTER cmsUInt8Number* output,
                                    CMSREGISTER cmsUInt32Number Stride)
{
    if (T_PLANAR(Info -> OutputFormat)) {

        cmsCIEXYZ XYZ;
        cmsFloat64Number* Out = (cmsFloat64Number*) output;
        cmsXYZEncoded2Float(ContextID, &XYZ, wOut);

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = XYZ.X;
        Out[Stride]   = XYZ.Y;
        Out[Stride*2] = XYZ.Z;

        return output + sizeof(cmsFloat64Number);

    }
    else {

        cmsXYZEncoded2Float(ContextID, (cmsCIEXYZ*) output, wOut);

        return output + (sizeof(cmsCIEXYZ) + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat64Number));
    }
}

static
cmsUInt8Number* PackXYZFloatFrom16(cmsContext ContextID,
                                   CMSREGISTER _cmsTRANSFORM* Info,
                                   CMSREGISTER cmsUInt16Number wOut[],
                                   CMSREGISTER cmsUInt8Number* output,
                                   CMSREGISTER cmsUInt32Number Stride)
{
    if (T_PLANAR(Info -> OutputFormat)) {

        cmsCIEXYZ XYZ;
        cmsFloat32Number* Out = (cmsFloat32Number*) output;
        cmsXYZEncoded2Float(ContextID, &XYZ, wOut);

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = (cmsFloat32Number) XYZ.X;
        Out[Stride]   = (cmsFloat32Number) XYZ.Y;
        Out[Stride*2] = (cmsFloat32Number) XYZ.Z;

        return output + sizeof(cmsFloat32Number);

    }
    else {

        cmsCIEXYZ XYZ;
        cmsFloat32Number* Out = (cmsFloat32Number*) output;
        cmsXYZEncoded2Float(ContextID, &XYZ, wOut);

        Out[0] = (cmsFloat32Number) XYZ.X;
        Out[1] = (cmsFloat32Number) XYZ.Y;
        Out[2] = (cmsFloat32Number) XYZ.Z;

        return output + (3 * sizeof(cmsFloat32Number) + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat32Number));
    }
}

static
cmsUInt8Number* PackDoubleFrom16(cmsContext ContextID,
                                 CMSREGISTER _cmsTRANSFORM* info,
                                 CMSREGISTER cmsUInt16Number wOut[],
                                 CMSREGISTER cmsUInt8Number* output,
                                 CMSREGISTER cmsUInt32Number Stride)
{
    cmsUInt32Number nChan      = T_CHANNELS(info -> OutputFormat);
    cmsUInt32Number DoSwap     = T_DOSWAP(info ->OutputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->OutputFormat);
    cmsUInt32Number Extra      = T_EXTRA(info -> OutputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> OutputFormat);
    cmsUInt32Number Planar     = T_PLANAR(info -> OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsFloat64Number maximum = IsInkSpace(info ->OutputFormat) ? 655.35 : 65535.0;
    cmsFloat64Number v = 0;
    cmsFloat64Number* swap1 = (cmsFloat64Number*) output;
    cmsUInt32Number i, start = 0;

    Stride /= PixelSize(info->OutputFormat);

    if (ExtraFirst)
        start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = (cmsFloat64Number) wOut[index] / maximum;

        if (Reverse)
            v = maximum - v;

        if (Planar)
            ((cmsFloat64Number*) output)[(i + start)  * Stride]= v;
        else
            ((cmsFloat64Number*) output)[i + start] = v;
    }


    if (Extra == 0 && SwapFirst) {

         memmove(swap1 + 1, swap1, (nChan-1)* sizeof(cmsFloat64Number));
        *swap1 = v;
    }

    if (T_PLANAR(info -> OutputFormat))
        return output + sizeof(cmsFloat64Number);
    else
        return output + (nChan + Extra) * sizeof(cmsFloat64Number);

}


static
cmsUInt8Number* PackFloatFrom16(cmsContext ContextID,
                                CMSREGISTER _cmsTRANSFORM* info,
                                CMSREGISTER cmsUInt16Number wOut[],
                                CMSREGISTER cmsUInt8Number* output,
                                CMSREGISTER cmsUInt32Number Stride)
{
       cmsUInt32Number nChan      = T_CHANNELS(info->OutputFormat);
       cmsUInt32Number DoSwap     = T_DOSWAP(info->OutputFormat);
       cmsUInt32Number Reverse    = T_FLAVOR(info->OutputFormat);
       cmsUInt32Number Extra      = T_EXTRA(info->OutputFormat);
       cmsUInt32Number SwapFirst  = T_SWAPFIRST(info->OutputFormat);
       cmsUInt32Number Planar     = T_PLANAR(info->OutputFormat);
       cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
       cmsFloat64Number maximum = IsInkSpace(info->OutputFormat) ? 655.35 : 65535.0;
       cmsFloat64Number v = 0;
       cmsFloat32Number* swap1 = (cmsFloat32Number*)output;
       cmsUInt32Number i, start = 0;

       Stride /= PixelSize(info->OutputFormat);

       if (ExtraFirst)
              start = Extra;

       for (i = 0; i < nChan; i++) {

              cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

              v = (cmsFloat64Number)wOut[index] / maximum;

              if (Reverse)
                     v = maximum - v;

              if (Planar)
                     ((cmsFloat32Number*)output)[(i + start) * Stride] = (cmsFloat32Number)v;
              else
                     ((cmsFloat32Number*)output)[i + start] = (cmsFloat32Number)v;
       }


       if (Extra == 0 && SwapFirst) {

              memmove(swap1 + 1, swap1, (nChan - 1)* sizeof(cmsFloat32Number));
              *swap1 = (cmsFloat32Number)v;
       }

       if (T_PLANAR(info->OutputFormat))
              return output + sizeof(cmsFloat32Number);
       else
              return output + (nChan + Extra) * sizeof(cmsFloat32Number);
}



// --------------------------------------------------------------------------------------------------------

static
cmsUInt8Number* PackBytesFromFloat(cmsContext ContextID,
				   _cmsTRANSFORM* info,
                                    cmsFloat32Number wOut[],
                                    cmsUInt8Number* output,
                                    cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Planar = T_PLANAR(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;    
    cmsUInt8Number* swap1 = (cmsUInt8Number*)output;
    cmsFloat64Number v = 0;
    cmsUInt8Number vv = 0;
    cmsUInt32Number i, start = 0;
    
    if (ExtraFirst)
        start = Extra;

    for (i = 0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = wOut[index] * 65535.0;

        if (Reverse)
            v = 65535.0 - v;
        
        vv =  FROM_16_TO_8(_cmsQuickSaturateWord(v));

        if (Planar)
            ((cmsUInt8Number*)output)[(i + start) * Stride] = vv;
        else
            ((cmsUInt8Number*)output)[i + start] = vv;
    }


    if (Extra == 0 && SwapFirst) {

        memmove(swap1 + 1, swap1, (nChan - 1) * sizeof(cmsUInt8Number));
        *swap1 = vv;
    }

    if (T_PLANAR(info->OutputFormat))
        return output + sizeof(cmsUInt8Number);
    else
        return output + (nChan + Extra) * sizeof(cmsUInt8Number);
}

static
cmsUInt8Number* PackWordsFromFloat(cmsContext ContextID,
				    _cmsTRANSFORM* info,
                                    cmsFloat32Number wOut[],
                                    cmsUInt8Number* output,
                                    cmsUInt32Number Stride)
{
    cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
    cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
    cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
    cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
    cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
    cmsUInt32Number Planar = T_PLANAR(info->OutputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;    
    cmsUInt16Number* swap1 = (cmsUInt16Number*)output;
    cmsFloat64Number v = 0;
    cmsUInt16Number vv = 0;
    cmsUInt32Number i, start = 0;
    
    if (ExtraFirst)
        start = Extra;

    for (i = 0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        v = wOut[index] * 65535.0;

        if (Reverse)
            v = 65535.0 - v;

        vv = _cmsQuickSaturateWord(v);

        if (Planar)
            ((cmsUInt16Number*)output)[(i + start) * Stride] = vv;
        else
            ((cmsUInt16Number*)output)[i + start] = vv;
    }

    if (Extra == 0 && SwapFirst) {

        memmove(swap1 + 1, swap1, (nChan - 1) * sizeof(cmsUInt16Number));
        *swap1 = vv;
    }

    if (T_PLANAR(info->OutputFormat))
        return output + sizeof(cmsUInt16Number);
    else
        return output + (nChan + Extra) * sizeof(cmsUInt16Number);
}


static
cmsUInt8Number* PackFloatsFromFloat(cmsContext ContextID,
				   _cmsTRANSFORM* info,
                                    cmsFloat32Number wOut[],
                                    cmsUInt8Number* output,
                                    cmsUInt32Number Stride)
{
       cmsUInt32Number nChan = T_CHANNELS(info->OutputFormat);
       cmsUInt32Number DoSwap = T_DOSWAP(info->OutputFormat);
       cmsUInt32Number Reverse = T_FLAVOR(info->OutputFormat);
       cmsUInt32Number Extra = T_EXTRA(info->OutputFormat);
       cmsUInt32Number SwapFirst = T_SWAPFIRST(info->OutputFormat);
       cmsUInt32Number Planar = T_PLANAR(info->OutputFormat);
       cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
       cmsFloat64Number maximum = IsInkSpace(info->OutputFormat) ? 100.0 : 1.0;
       cmsFloat32Number* swap1 = (cmsFloat32Number*)output;
       cmsFloat64Number v = 0;
       cmsUInt32Number i, start = 0;

       Stride /= PixelSize(info->OutputFormat);

       if (ExtraFirst)
              start = Extra;

       for (i = 0; i < nChan; i++) {

              cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

              v = wOut[index] * maximum;

              if (Reverse)
                     v = maximum - v;

              if (Planar)
                     ((cmsFloat32Number*)output)[(i + start)* Stride] = (cmsFloat32Number)v;
              else
                     ((cmsFloat32Number*)output)[i + start] = (cmsFloat32Number)v;
       }


       if (Extra == 0 && SwapFirst) {

              memmove(swap1 + 1, swap1, (nChan - 1)* sizeof(cmsFloat32Number));
              *swap1 = (cmsFloat32Number)v;
       }

       if (T_PLANAR(info->OutputFormat))
              return output + sizeof(cmsFloat32Number);
       else
              return output + (nChan + Extra) * sizeof(cmsFloat32Number);
}

static
cmsUInt8Number* PackDoublesFromFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                    cmsFloat32Number wOut[],
                                    cmsUInt8Number* output,
                                    cmsUInt32Number Stride)
{
       cmsUInt32Number nChan      = T_CHANNELS(info->OutputFormat);
       cmsUInt32Number DoSwap     = T_DOSWAP(info->OutputFormat);
       cmsUInt32Number Reverse    = T_FLAVOR(info->OutputFormat);
       cmsUInt32Number Extra      = T_EXTRA(info->OutputFormat);
       cmsUInt32Number SwapFirst  = T_SWAPFIRST(info->OutputFormat);
       cmsUInt32Number Planar     = T_PLANAR(info->OutputFormat);
       cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
       cmsFloat64Number maximum = IsInkSpace(info->OutputFormat) ? 100.0 : 1.0;
       cmsFloat64Number v = 0;
       cmsFloat64Number* swap1 = (cmsFloat64Number*)output;
       cmsUInt32Number i, start = 0;

       Stride /= PixelSize(info->OutputFormat);

       if (ExtraFirst)
              start = Extra;

       for (i = 0; i < nChan; i++) {

              cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

              v = wOut[index] * maximum;

              if (Reverse)
                     v = maximum - v;

              if (Planar)
                     ((cmsFloat64Number*)output)[(i + start) * Stride] = v;
              else
                     ((cmsFloat64Number*)output)[i + start] = v;
       }

       if (Extra == 0 && SwapFirst) {

              memmove(swap1 + 1, swap1, (nChan - 1)* sizeof(cmsFloat64Number));
              *swap1 = v;
       }


       if (T_PLANAR(info->OutputFormat))
              return output + sizeof(cmsFloat64Number);
       else
              return output + (nChan + Extra) * sizeof(cmsFloat64Number);

}

static
cmsUInt8Number* PackLabFloatFromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                      cmsFloat32Number wOut[],
                                      cmsUInt8Number* output,
                                      cmsUInt32Number Stride)
{
    cmsFloat32Number* Out = (cmsFloat32Number*) output;

    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = (cmsFloat32Number) (wOut[0] * 100.0);
        Out[Stride]   = (cmsFloat32Number) (wOut[1] * 255.0 - 128.0);
        Out[Stride*2] = (cmsFloat32Number) (wOut[2] * 255.0 - 128.0);

        return output + sizeof(cmsFloat32Number);
    }
    else {

        Out[0] = (cmsFloat32Number) (wOut[0] * 100.0);
        Out[1] = (cmsFloat32Number) (wOut[1] * 255.0 - 128.0);
        Out[2] = (cmsFloat32Number) (wOut[2] * 255.0 - 128.0);

        return output + (sizeof(cmsFloat32Number)*3 + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat32Number));
    }

}


static
cmsUInt8Number* PackLabDoubleFromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                       cmsFloat32Number wOut[],
                                       cmsUInt8Number* output,
                                       cmsUInt32Number Stride)
{
    cmsFloat64Number* Out = (cmsFloat64Number*) output;

    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = (cmsFloat64Number) (wOut[0] * 100.0);
        Out[Stride]   = (cmsFloat64Number) (wOut[1] * 255.0 - 128.0);
        Out[Stride*2] = (cmsFloat64Number) (wOut[2] * 255.0 - 128.0);

        return output + sizeof(cmsFloat64Number);
    }
    else {

        Out[0] = (cmsFloat64Number) (wOut[0] * 100.0);
        Out[1] = (cmsFloat64Number) (wOut[1] * 255.0 - 128.0);
        Out[2] = (cmsFloat64Number) (wOut[2] * 255.0 - 128.0);

        return output + (sizeof(cmsFloat64Number)*3 + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat64Number));
    }

}


static
cmsUInt8Number* PackEncodedBytesLabV2FromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                           cmsFloat32Number wOut[],
                                           cmsUInt8Number* output,
                                           cmsUInt32Number Stride)
{    
    cmsCIELab Lab;
    cmsUInt16Number wlab[3];

    Lab.L = (cmsFloat64Number)(wOut[0] * 100.0);
    Lab.a = (cmsFloat64Number)(wOut[1] * 255.0 - 128.0);
    Lab.b = (cmsFloat64Number)(wOut[2] * 255.0 - 128.0);

    cmsFloat2LabEncoded(ContextID, wlab, &Lab);
    
    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);       
    
        output[0]        = wlab[0] >> 8;
        output[Stride]   = wlab[1] >> 8;
        output[Stride*2] = wlab[2] >> 8;

        return output + 1;
    }
    else {

        output[0] = wlab[0] >> 8;
        output[1] = wlab[1] >> 8;
        output[2] = wlab[2] >> 8;

        return output + (3 + T_EXTRA(Info ->OutputFormat));
    }
}

static
cmsUInt8Number* PackEncodedWordsLabV2FromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                           cmsFloat32Number wOut[],
                                           cmsUInt8Number* output,
                                           cmsUInt32Number Stride)
{    
    cmsCIELab Lab;
    cmsUInt16Number wlab[3];

    Lab.L = (cmsFloat64Number)(wOut[0] * 100.0);
    Lab.a = (cmsFloat64Number)(wOut[1] * 255.0 - 128.0);
    Lab.b = (cmsFloat64Number)(wOut[2] * 255.0 - 128.0);

    cmsFloat2LabEncodedV2(ContextID, wlab, &Lab);
    
    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);       
    
        ((cmsUInt16Number*) output)[0]        = wlab[0];
        ((cmsUInt16Number*) output)[Stride]   = wlab[1];
        ((cmsUInt16Number*) output)[Stride*2] = wlab[2];

        return output + sizeof(cmsUInt16Number);
    }
    else {

         ((cmsUInt16Number*) output)[0] = wlab[0];
         ((cmsUInt16Number*) output)[1] = wlab[1];
         ((cmsUInt16Number*) output)[2] = wlab[2];

        return output + (3 + T_EXTRA(Info ->OutputFormat)) * sizeof(cmsUInt16Number);
    }
}


// From 0..1 range to 0..MAX_ENCODEABLE_XYZ
static
cmsUInt8Number* PackXYZFloatFromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                      cmsFloat32Number wOut[],
                                      cmsUInt8Number* output,
                                      cmsUInt32Number Stride)
{
    cmsFloat32Number* Out = (cmsFloat32Number*) output;

    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = (cmsFloat32Number) (wOut[0] * MAX_ENCODEABLE_XYZ);
        Out[Stride]   = (cmsFloat32Number) (wOut[1] * MAX_ENCODEABLE_XYZ);
        Out[Stride*2] = (cmsFloat32Number) (wOut[2] * MAX_ENCODEABLE_XYZ);

        return output + sizeof(cmsFloat32Number);
    }
    else {

        Out[0] = (cmsFloat32Number) (wOut[0] * MAX_ENCODEABLE_XYZ);
        Out[1] = (cmsFloat32Number) (wOut[1] * MAX_ENCODEABLE_XYZ);
        Out[2] = (cmsFloat32Number) (wOut[2] * MAX_ENCODEABLE_XYZ);

        return output + (sizeof(cmsFloat32Number)*3 + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat32Number));
    }

}

// Same, but convert to double
static
cmsUInt8Number* PackXYZDoubleFromFloat(cmsContext ContextID, _cmsTRANSFORM* Info,
                                       cmsFloat32Number wOut[],
                                       cmsUInt8Number* output,
                                       cmsUInt32Number Stride)
{
    cmsFloat64Number* Out = (cmsFloat64Number*) output;

    if (T_PLANAR(Info -> OutputFormat)) {

        Stride /= PixelSize(Info->OutputFormat);

        Out[0]        = (cmsFloat64Number) (wOut[0] * MAX_ENCODEABLE_XYZ);
        Out[Stride]   = (cmsFloat64Number) (wOut[1] * MAX_ENCODEABLE_XYZ);
        Out[Stride*2] = (cmsFloat64Number) (wOut[2] * MAX_ENCODEABLE_XYZ);

        return output + sizeof(cmsFloat64Number);
    }
    else {

        Out[0] = (cmsFloat64Number) (wOut[0] * MAX_ENCODEABLE_XYZ);
        Out[1] = (cmsFloat64Number) (wOut[1] * MAX_ENCODEABLE_XYZ);
        Out[2] = (cmsFloat64Number) (wOut[2] * MAX_ENCODEABLE_XYZ);

        return output + (sizeof(cmsFloat64Number)*3 + T_EXTRA(Info ->OutputFormat) * sizeof(cmsFloat64Number));
    }

}


// ----------------------------------------------------------------------------------------------------------------

#ifndef CMS_NO_HALF_SUPPORT

// Decodes an stream of half floats to wIn[] described by input format

static
cmsUInt8Number* UnrollHalfTo16(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wIn[],
                               CMSREGISTER cmsUInt8Number* accum,
                               CMSREGISTER cmsUInt32Number Stride)
{

    cmsUInt32Number nChan      = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap     = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> InputFormat);
    cmsUInt32Number Extra      = T_EXTRA(info -> InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar     = T_PLANAR(info -> InputFormat);
    cmsFloat32Number v;
    cmsUInt32Number i, start = 0;
    cmsFloat32Number maximum = IsInkSpace(info ->InputFormat) ? 655.35F : 65535.0F;


    Stride /= PixelSize(info->OutputFormat);

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v = _cmsHalf2Float ( ((cmsUInt16Number*) accum)[(i + start) * Stride] );
        else
            v = _cmsHalf2Float ( ((cmsUInt16Number*) accum)[i + start] ) ;

        if (Reverse) v = maximum - v;

        wIn[index] = _cmsQuickSaturateWord((cmsFloat64Number) v * maximum);
    }


    if (Extra == 0 && SwapFirst) {
        cmsUInt16Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsUInt16Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsUInt16Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsUInt16Number);
}

// Decodes an stream of half floats to wIn[] described by input format

static
cmsUInt8Number* UnrollHalfToFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                    cmsFloat32Number wIn[],
                                    cmsUInt8Number* accum,
                                    cmsUInt32Number Stride)
{

    cmsUInt32Number nChan      = T_CHANNELS(info -> InputFormat);
    cmsUInt32Number DoSwap     = T_DOSWAP(info ->InputFormat);
    cmsUInt32Number Reverse    = T_FLAVOR(info ->InputFormat);
    cmsUInt32Number SwapFirst  = T_SWAPFIRST(info -> InputFormat);
    cmsUInt32Number Extra      = T_EXTRA(info -> InputFormat);
    cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
    cmsUInt32Number Planar     = T_PLANAR(info -> InputFormat);
    cmsFloat32Number v;
    cmsUInt32Number i, start = 0;
    cmsFloat32Number maximum = IsInkSpace(info ->InputFormat) ? 100.0F : 1.0F;

    Stride /= PixelSize(info->OutputFormat);

    if (ExtraFirst)
            start = Extra;

    for (i=0; i < nChan; i++) {

        cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

        if (Planar)
            v =  _cmsHalf2Float ( ((cmsUInt16Number*) accum)[(i + start) * Stride] );
        else
            v =  _cmsHalf2Float ( ((cmsUInt16Number*) accum)[i + start] ) ;

        v /= maximum;

        wIn[index] = Reverse ? 1 - v : v;
    }


    if (Extra == 0 && SwapFirst) {
        cmsFloat32Number tmp = wIn[0];

        memmove(&wIn[0], &wIn[1], (nChan-1) * sizeof(cmsFloat32Number));
        wIn[nChan-1] = tmp;
    }

    if (T_PLANAR(info -> InputFormat))
        return accum + sizeof(cmsUInt16Number);
    else
        return accum + (nChan + Extra) * sizeof(cmsUInt16Number);
}


static
cmsUInt8Number* PackHalfFrom16(cmsContext ContextID,
                               CMSREGISTER _cmsTRANSFORM* info,
                               CMSREGISTER cmsUInt16Number wOut[],
                               CMSREGISTER cmsUInt8Number* output,
                               CMSREGISTER cmsUInt32Number Stride)
{
       cmsUInt32Number nChan      = T_CHANNELS(info->OutputFormat);
       cmsUInt32Number DoSwap     = T_DOSWAP(info->OutputFormat);
       cmsUInt32Number Reverse    = T_FLAVOR(info->OutputFormat);
       cmsUInt32Number Extra      = T_EXTRA(info->OutputFormat);
       cmsUInt32Number SwapFirst  = T_SWAPFIRST(info->OutputFormat);
       cmsUInt32Number Planar     = T_PLANAR(info->OutputFormat);
       cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
       cmsFloat32Number maximum = IsInkSpace(info->OutputFormat) ? 655.35F : 65535.0F;
       cmsFloat32Number v = 0;
       cmsUInt16Number* swap1 = (cmsUInt16Number*)output;
       cmsUInt32Number i, start = 0;

       Stride /= PixelSize(info->OutputFormat);

       if (ExtraFirst)
              start = Extra;

       for (i = 0; i < nChan; i++) {

              cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

              v = (cmsFloat32Number)wOut[index] / maximum;

              if (Reverse)
                     v = maximum - v;

              if (Planar)
                     ((cmsUInt16Number*)output)[(i + start) * Stride] = _cmsFloat2Half(v);
              else
                     ((cmsUInt16Number*)output)[i + start] = _cmsFloat2Half(v);
       }


       if (Extra == 0 && SwapFirst) {

              memmove(swap1 + 1, swap1, (nChan - 1)* sizeof(cmsUInt16Number));
              *swap1 = _cmsFloat2Half(v);
       }

       if (T_PLANAR(info->OutputFormat))
              return output + sizeof(cmsUInt16Number);
       else
              return output + (nChan + Extra) * sizeof(cmsUInt16Number);
}



static
cmsUInt8Number* PackHalfFromFloat(cmsContext ContextID, _cmsTRANSFORM* info,
                                    cmsFloat32Number wOut[],
                                    cmsUInt8Number* output,
                                    cmsUInt32Number Stride)
{
       cmsUInt32Number nChan      = T_CHANNELS(info->OutputFormat);
       cmsUInt32Number DoSwap     = T_DOSWAP(info->OutputFormat);
       cmsUInt32Number Reverse    = T_FLAVOR(info->OutputFormat);
       cmsUInt32Number Extra      = T_EXTRA(info->OutputFormat);
       cmsUInt32Number SwapFirst  = T_SWAPFIRST(info->OutputFormat);
       cmsUInt32Number Planar     = T_PLANAR(info->OutputFormat);
       cmsUInt32Number ExtraFirst = DoSwap ^ SwapFirst;
       cmsFloat32Number maximum = IsInkSpace(info->OutputFormat) ? 100.0F : 1.0F;
       cmsUInt16Number* swap1 = (cmsUInt16Number*)output;
       cmsFloat32Number v = 0;
       cmsUInt32Number i, start = 0;

       Stride /= PixelSize(info->OutputFormat);

       if (ExtraFirst)
              start = Extra;

       for (i = 0; i < nChan; i++) {

           cmsUInt32Number index = DoSwap ? (nChan - i - 1) : i;

              v = wOut[index] * maximum;

              if (Reverse)
                     v = maximum - v;

              if (Planar)
                     ((cmsUInt16Number*)output)[(i + start)* Stride] = _cmsFloat2Half(v);
              else
                     ((cmsUInt16Number*)output)[i + start] = _cmsFloat2Half(v);
       }


       if (Extra == 0 && SwapFirst) {

              memmove(swap1 + 1, swap1, (nChan - 1)* sizeof(cmsUInt16Number));
              *swap1 = (cmsUInt16Number)_cmsFloat2Half(v);
       }

       if (T_PLANAR(info->OutputFormat))
              return output + sizeof(cmsUInt16Number);
       else
              return output + (nChan + Extra)* sizeof(cmsUInt16Number);
}

#endif

// ----------------------------------------------------------------------------------------------------------------


static const cmsFormatters16 InputFormatters16[] = {

    //    Type                                          Mask                  Function
    //  ----------------------------   ------------------------------------  ----------------------------
    { TYPE_Lab_DBL,                                 ANYPLANAR|ANYEXTRA,   UnrollLabDoubleTo16},
    { TYPE_XYZ_DBL,                                 ANYPLANAR|ANYEXTRA,   UnrollXYZDoubleTo16},
    { TYPE_Lab_FLT,                                 ANYPLANAR|ANYEXTRA,   UnrollLabFloatTo16},
    { TYPE_XYZ_FLT,                                 ANYPLANAR|ANYEXTRA,   UnrollXYZFloatTo16},
    { TYPE_GRAY_DBL,                                                 0,   UnrollDouble1Chan},
    { FLOAT_SH(1)|BYTES_SH(0), ANYCHANNELS|ANYPLANAR|ANYSWAPFIRST|ANYFLAVOR|
                                             ANYSWAP|ANYEXTRA|ANYSPACE,   UnrollDoubleTo16},
    { FLOAT_SH(1)|BYTES_SH(4), ANYCHANNELS|ANYPLANAR|ANYSWAPFIRST|ANYFLAVOR|
                                             ANYSWAP|ANYEXTRA|ANYSPACE,   UnrollFloatTo16},
#ifndef CMS_NO_HALF_SUPPORT
    { FLOAT_SH(1)|BYTES_SH(2), ANYCHANNELS|ANYPLANAR|ANYSWAPFIRST|ANYFLAVOR|
                                            ANYEXTRA|ANYSWAP|ANYSPACE,   UnrollHalfTo16},
#endif

    { CHANNELS_SH(1)|BYTES_SH(1),                              ANYSPACE,  Unroll1Byte},
    { CHANNELS_SH(1)|BYTES_SH(1)|EXTRA_SH(1),                  ANYSPACE,  Unroll1ByteSkip1},
    { CHANNELS_SH(1)|BYTES_SH(1)|EXTRA_SH(2),                  ANYSPACE,  Unroll1ByteSkip2},
    { CHANNELS_SH(1)|BYTES_SH(1)|FLAVOR_SH(1),                 ANYSPACE,  Unroll1ByteReversed},
    { COLORSPACE_SH(PT_MCH2)|CHANNELS_SH(2)|BYTES_SH(1),              0,  Unroll2Bytes},

    { TYPE_LabV2_8,                                                   0,  UnrollLabV2_8 },
    { TYPE_ALabV2_8,                                                  0,  UnrollALabV2_8 },
    { TYPE_LabV2_16,                                                  0,  UnrollLabV2_16 },

    { CHANNELS_SH(3)|BYTES_SH(1),                              ANYSPACE,  Unroll3Bytes},
    { CHANNELS_SH(3)|BYTES_SH(1)|DOSWAP_SH(1),                 ANYSPACE,  Unroll3BytesSwap},
    { CHANNELS_SH(3)|EXTRA_SH(1)|BYTES_SH(1)|DOSWAP_SH(1),     ANYSPACE,  Unroll3BytesSkip1Swap},
    { CHANNELS_SH(3)|EXTRA_SH(1)|BYTES_SH(1)|SWAPFIRST_SH(1),  ANYSPACE,  Unroll3BytesSkip1SwapFirst},

    { CHANNELS_SH(3)|EXTRA_SH(1)|BYTES_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1),
                                                               ANYSPACE,  Unroll3BytesSkip1SwapSwapFirst},

    { CHANNELS_SH(4)|BYTES_SH(1),                              ANYSPACE,  Unroll4Bytes},
    { CHANNELS_SH(4)|BYTES_SH(1)|FLAVOR_SH(1),                 ANYSPACE,  Unroll4BytesReverse},
    { CHANNELS_SH(4)|BYTES_SH(1)|SWAPFIRST_SH(1),              ANYSPACE,  Unroll4BytesSwapFirst},
    { CHANNELS_SH(4)|BYTES_SH(1)|DOSWAP_SH(1),                 ANYSPACE,  Unroll4BytesSwap},
    { CHANNELS_SH(4)|BYTES_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1), ANYSPACE,  Unroll4BytesSwapSwapFirst},

    { BYTES_SH(1)|PLANAR_SH(1), ANYFLAVOR|ANYSWAPFIRST|ANYPREMUL|
                                   ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE, UnrollPlanarBytes},

    { BYTES_SH(1),    ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYPREMUL|
                                           ANYEXTRA|ANYCHANNELS|ANYSPACE, UnrollChunkyBytes},

    { CHANNELS_SH(1)|BYTES_SH(2),                              ANYSPACE,  Unroll1Word},
    { CHANNELS_SH(1)|BYTES_SH(2)|FLAVOR_SH(1),                 ANYSPACE,  Unroll1WordReversed},
    { CHANNELS_SH(1)|BYTES_SH(2)|EXTRA_SH(3),                  ANYSPACE,  Unroll1WordSkip3},

    { CHANNELS_SH(2)|BYTES_SH(2),                              ANYSPACE,  Unroll2Words},
    { CHANNELS_SH(3)|BYTES_SH(2),                              ANYSPACE,  Unroll3Words},
    { CHANNELS_SH(4)|BYTES_SH(2),                              ANYSPACE,  Unroll4Words},

    { CHANNELS_SH(3)|BYTES_SH(2)|DOSWAP_SH(1),                 ANYSPACE,  Unroll3WordsSwap},
    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1)|SWAPFIRST_SH(1),  ANYSPACE,  Unroll3WordsSkip1SwapFirst},
    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1)|DOSWAP_SH(1),     ANYSPACE,  Unroll3WordsSkip1Swap},
    { CHANNELS_SH(4)|BYTES_SH(2)|FLAVOR_SH(1),                 ANYSPACE,  Unroll4WordsReverse},
    { CHANNELS_SH(4)|BYTES_SH(2)|SWAPFIRST_SH(1),              ANYSPACE,  Unroll4WordsSwapFirst},
    { CHANNELS_SH(4)|BYTES_SH(2)|DOSWAP_SH(1),                 ANYSPACE,  Unroll4WordsSwap},
    { CHANNELS_SH(4)|BYTES_SH(2)|DOSWAP_SH(1)|SWAPFIRST_SH(1), ANYSPACE,  Unroll4WordsSwapSwapFirst},


    { BYTES_SH(2)|PLANAR_SH(1),  ANYFLAVOR|ANYSWAP|ANYENDIAN|ANYEXTRA|ANYCHANNELS|ANYSPACE,  UnrollPlanarWords},
    { BYTES_SH(2),  ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYENDIAN|ANYEXTRA|ANYCHANNELS|ANYSPACE,  UnrollAnyWords},

    { BYTES_SH(2)|PLANAR_SH(1),  ANYFLAVOR|ANYSWAP|ANYENDIAN|ANYEXTRA|ANYCHANNELS|ANYSPACE|PREMUL_SH(1),  UnrollPlanarWordsPremul},
    { BYTES_SH(2),  ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYENDIAN|ANYEXTRA|ANYCHANNELS|ANYSPACE|PREMUL_SH(1),  UnrollAnyWordsPremul}

};



static const cmsFormattersFloat InputFormattersFloat[] = {

    //    Type                                          Mask                  Function
    //  ----------------------------   ------------------------------------  ----------------------------
    {     TYPE_Lab_DBL,                                ANYPLANAR|ANYEXTRA,   UnrollLabDoubleToFloat},
    {     TYPE_Lab_FLT,                                ANYPLANAR|ANYEXTRA,   UnrollLabFloatToFloat},

    {     TYPE_XYZ_DBL,                                ANYPLANAR|ANYEXTRA,   UnrollXYZDoubleToFloat},
    {     TYPE_XYZ_FLT,                                ANYPLANAR|ANYEXTRA,   UnrollXYZFloatToFloat},

    {     FLOAT_SH(1)|BYTES_SH(4), ANYPLANAR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                            ANYPREMUL|ANYCHANNELS|ANYSPACE,  UnrollFloatsToFloat},

    {     FLOAT_SH(1)|BYTES_SH(0), ANYPLANAR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                              ANYCHANNELS|ANYSPACE|ANYPREMUL, UnrollDoublesToFloat},

    {     TYPE_LabV2_8,                                                   0,  UnrollLabV2_8ToFloat },
    {     TYPE_ALabV2_8,                                                  0,  UnrollALabV2_8ToFloat },
    {     TYPE_LabV2_16,                                                  0,  UnrollLabV2_16ToFloat },

    {     BYTES_SH(1),              ANYPLANAR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                                        ANYCHANNELS|ANYSPACE, Unroll8ToFloat},

    {     BYTES_SH(2),              ANYPLANAR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                                        ANYCHANNELS|ANYSPACE, Unroll16ToFloat},
#ifndef CMS_NO_HALF_SUPPORT
    {     FLOAT_SH(1)|BYTES_SH(2), ANYPLANAR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                                        ANYCHANNELS|ANYSPACE, UnrollHalfToFloat},
#endif
};


// Bit fields set to one in the mask are not compared
static
cmsFormatter _cmsGetStockInputFormatter(cmsUInt32Number dwInput, cmsUInt32Number dwFlags)
{
    cmsUInt32Number i;
    cmsFormatter fr;

    switch (dwFlags) {

    case CMS_PACK_FLAGS_16BITS: {
        for (i=0; i < sizeof(InputFormatters16) / sizeof(cmsFormatters16); i++) {
            const cmsFormatters16* f = InputFormatters16 + i;

            if ((dwInput & ~f ->Mask) == f ->Type) {
                fr.Fmt16 = f ->Frm;
                return fr;
            }
        }
    }
    break;

    case CMS_PACK_FLAGS_FLOAT: {
        for (i=0; i < sizeof(InputFormattersFloat) / sizeof(cmsFormattersFloat); i++) {
            const cmsFormattersFloat* f = InputFormattersFloat + i;

            if ((dwInput & ~f ->Mask) == f ->Type) {
                fr.FmtFloat = f ->Frm;
                return fr;
            }
        }
    }
    break;

    default:;

    }

    fr.Fmt16 = NULL;
    return fr;
}

static const cmsFormatters16 OutputFormatters16[] = {
    //    Type                                          Mask                  Function
    //  ----------------------------   ------------------------------------  ----------------------------

    { TYPE_Lab_DBL,                                      ANYPLANAR|ANYEXTRA,  PackLabDoubleFrom16},
    { TYPE_XYZ_DBL,                                      ANYPLANAR|ANYEXTRA,  PackXYZDoubleFrom16},

    { TYPE_Lab_FLT,                                      ANYPLANAR|ANYEXTRA,  PackLabFloatFrom16},
    { TYPE_XYZ_FLT,                                      ANYPLANAR|ANYEXTRA,  PackXYZFloatFrom16},

    { FLOAT_SH(1)|BYTES_SH(0),      ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|
                                    ANYCHANNELS|ANYPLANAR|ANYEXTRA|ANYSPACE,  PackDoubleFrom16},
    { FLOAT_SH(1)|BYTES_SH(4),      ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|
                                    ANYCHANNELS|ANYPLANAR|ANYEXTRA|ANYSPACE,  PackFloatFrom16},
#ifndef CMS_NO_HALF_SUPPORT
    { FLOAT_SH(1)|BYTES_SH(2),      ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|
                                    ANYCHANNELS|ANYPLANAR|ANYEXTRA|ANYSPACE,  PackHalfFrom16},
#endif

    { CHANNELS_SH(1)|BYTES_SH(1),                                  ANYSPACE,  Pack1Byte},
    { CHANNELS_SH(1)|BYTES_SH(1)|EXTRA_SH(1),                      ANYSPACE,  Pack1ByteSkip1},
    { CHANNELS_SH(1)|BYTES_SH(1)|EXTRA_SH(1)|SWAPFIRST_SH(1),      ANYSPACE,  Pack1ByteSkip1SwapFirst},

    { CHANNELS_SH(1)|BYTES_SH(1)|FLAVOR_SH(1),                     ANYSPACE,  Pack1ByteReversed},

    { TYPE_LabV2_8,                                                       0,  PackLabV2_8 },
    { TYPE_ALabV2_8,                                                      0,  PackALabV2_8 },
    { TYPE_LabV2_16,                                                      0,  PackLabV2_16 },

    { CHANNELS_SH(3)|BYTES_SH(1)|OPTIMIZED_SH(1),                  ANYSPACE,  Pack3BytesOptimized},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1)|OPTIMIZED_SH(1),      ANYSPACE,  Pack3BytesAndSkip1Optimized},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1)|SWAPFIRST_SH(1)|OPTIMIZED_SH(1),
                                                                   ANYSPACE,  Pack3BytesAndSkip1SwapFirstOptimized},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1)|OPTIMIZED_SH(1),
                                                                   ANYSPACE,  Pack3BytesAndSkip1SwapSwapFirstOptimized},
    { CHANNELS_SH(3)|BYTES_SH(1)|DOSWAP_SH(1)|EXTRA_SH(1)|OPTIMIZED_SH(1),
                                                                   ANYSPACE,  Pack3BytesAndSkip1SwapOptimized},
    { CHANNELS_SH(3)|BYTES_SH(1)|DOSWAP_SH(1)|OPTIMIZED_SH(1),     ANYSPACE,  Pack3BytesSwapOptimized},



    { CHANNELS_SH(3)|BYTES_SH(1),                                  ANYSPACE,  Pack3Bytes},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1),                      ANYSPACE,  Pack3BytesAndSkip1},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1)|SWAPFIRST_SH(1),      ANYSPACE,  Pack3BytesAndSkip1SwapFirst},
    { CHANNELS_SH(3)|BYTES_SH(1)|EXTRA_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1),
                                                                   ANYSPACE,  Pack3BytesAndSkip1SwapSwapFirst},
    { CHANNELS_SH(3)|BYTES_SH(1)|DOSWAP_SH(1)|EXTRA_SH(1),         ANYSPACE,  Pack3BytesAndSkip1Swap},
    { CHANNELS_SH(3)|BYTES_SH(1)|DOSWAP_SH(1),                     ANYSPACE,  Pack3BytesSwap},
    { CHANNELS_SH(4)|BYTES_SH(1),                                  ANYSPACE,  Pack4Bytes},
    { CHANNELS_SH(4)|BYTES_SH(1)|FLAVOR_SH(1),                     ANYSPACE,  Pack4BytesReverse},
    { CHANNELS_SH(4)|BYTES_SH(1)|SWAPFIRST_SH(1),                  ANYSPACE,  Pack4BytesSwapFirst},
    { CHANNELS_SH(4)|BYTES_SH(1)|DOSWAP_SH(1),                     ANYSPACE,  Pack4BytesSwap},
    { CHANNELS_SH(4)|BYTES_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1),     ANYSPACE,  Pack4BytesSwapSwapFirst},
    { CHANNELS_SH(6)|BYTES_SH(1),                                  ANYSPACE,  Pack6Bytes},
    { CHANNELS_SH(6)|BYTES_SH(1)|DOSWAP_SH(1),                     ANYSPACE,  Pack6BytesSwap},

    { BYTES_SH(1),    ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|
                                                          ANYSPACE|ANYPREMUL, PackChunkyBytes},

    { BYTES_SH(1)|PLANAR_SH(1),    ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|
                                              ANYCHANNELS|ANYSPACE|ANYPREMUL, PackPlanarBytes},


    { CHANNELS_SH(1)|BYTES_SH(2),                                  ANYSPACE,  Pack1Word},
    { CHANNELS_SH(1)|BYTES_SH(2)|EXTRA_SH(1),                      ANYSPACE,  Pack1WordSkip1},
    { CHANNELS_SH(1)|BYTES_SH(2)|EXTRA_SH(1)|SWAPFIRST_SH(1),      ANYSPACE,  Pack1WordSkip1SwapFirst},
    { CHANNELS_SH(1)|BYTES_SH(2)|FLAVOR_SH(1),                     ANYSPACE,  Pack1WordReversed},
    { CHANNELS_SH(1)|BYTES_SH(2)|ENDIAN16_SH(1),                   ANYSPACE,  Pack1WordBigEndian},
    { CHANNELS_SH(3)|BYTES_SH(2),                                  ANYSPACE,  Pack3Words},
    { CHANNELS_SH(3)|BYTES_SH(2)|DOSWAP_SH(1),                     ANYSPACE,  Pack3WordsSwap},
    { CHANNELS_SH(3)|BYTES_SH(2)|ENDIAN16_SH(1),                   ANYSPACE,  Pack3WordsBigEndian},
    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1),                      ANYSPACE,  Pack3WordsAndSkip1},
    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1)|DOSWAP_SH(1),         ANYSPACE,  Pack3WordsAndSkip1Swap},
    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1)|SWAPFIRST_SH(1),      ANYSPACE,  Pack3WordsAndSkip1SwapFirst},

    { CHANNELS_SH(3)|BYTES_SH(2)|EXTRA_SH(1)|DOSWAP_SH(1)|SWAPFIRST_SH(1),
                                                                   ANYSPACE,  Pack3WordsAndSkip1SwapSwapFirst},

    { CHANNELS_SH(4)|BYTES_SH(2),                                  ANYSPACE,  Pack4Words},
    { CHANNELS_SH(4)|BYTES_SH(2)|FLAVOR_SH(1),                     ANYSPACE,  Pack4WordsReverse},
    { CHANNELS_SH(4)|BYTES_SH(2)|DOSWAP_SH(1),                     ANYSPACE,  Pack4WordsSwap},
    { CHANNELS_SH(4)|BYTES_SH(2)|ENDIAN16_SH(1),                   ANYSPACE,  Pack4WordsBigEndian},

    { CHANNELS_SH(6)|BYTES_SH(2),                                  ANYSPACE,  Pack6Words},
    { CHANNELS_SH(6)|BYTES_SH(2)|DOSWAP_SH(1),                     ANYSPACE,  Pack6WordsSwap},

    { BYTES_SH(2),                  ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYENDIAN|
                                     ANYEXTRA|ANYCHANNELS|ANYSPACE|ANYPREMUL, PackChunkyWords},
    { BYTES_SH(2)|PLANAR_SH(1),     ANYFLAVOR|ANYENDIAN|ANYSWAP|ANYEXTRA|
                                     ANYCHANNELS|ANYSPACE|ANYPREMUL,          PackPlanarWords}

};


static const cmsFormattersFloat OutputFormattersFloat[] = {
    //    Type                                          Mask                                 Function
    //  ----------------------------   ---------------------------------------------------  ----------------------------
    {     TYPE_Lab_FLT,                                                ANYPLANAR|ANYEXTRA,   PackLabFloatFromFloat},
    {     TYPE_XYZ_FLT,                                                ANYPLANAR|ANYEXTRA,   PackXYZFloatFromFloat},

    {     TYPE_Lab_DBL,                                                ANYPLANAR|ANYEXTRA,   PackLabDoubleFromFloat},
    {     TYPE_XYZ_DBL,                                                ANYPLANAR|ANYEXTRA,   PackXYZDoubleFromFloat},

    {     TYPE_LabV2_8,                                                ANYPLANAR|ANYEXTRA,   PackEncodedBytesLabV2FromFloat},
    {     TYPE_LabV2_16,                                               ANYPLANAR|ANYEXTRA,   PackEncodedWordsLabV2FromFloat},

    {     FLOAT_SH(1)|BYTES_SH(4), ANYPLANAR|
                             ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE,   PackFloatsFromFloat },
    {     FLOAT_SH(1)|BYTES_SH(0), ANYPLANAR|
                             ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE,   PackDoublesFromFloat },

    {     BYTES_SH(2), ANYPLANAR|
                             ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE,   PackWordsFromFloat },

    {     BYTES_SH(1), ANYPLANAR|
                             ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE,   PackBytesFromFloat },

#ifndef CMS_NO_HALF_SUPPORT
    {     FLOAT_SH(1)|BYTES_SH(2),
                             ANYFLAVOR|ANYSWAPFIRST|ANYSWAP|ANYEXTRA|ANYCHANNELS|ANYSPACE,   PackHalfFromFloat },
#endif

};


// Bit fields set to one in the mask are not compared
static
cmsFormatter _cmsGetStockOutputFormatter(cmsUInt32Number dwInput, cmsUInt32Number dwFlags)
{
    cmsUInt32Number i;
    cmsFormatter fr;

    // Optimization is only a hint
    dwInput &= ~OPTIMIZED_SH(1);

    switch (dwFlags)
    {

     case CMS_PACK_FLAGS_16BITS: {

        for (i=0; i < sizeof(OutputFormatters16) / sizeof(cmsFormatters16); i++) {
            const cmsFormatters16* f = OutputFormatters16 + i;

            if ((dwInput & ~f ->Mask) == f ->Type) {
                fr.Fmt16 = f ->Frm;
                return fr;
            }
        }
        }
        break;

    case CMS_PACK_FLAGS_FLOAT: {

        for (i=0; i < sizeof(OutputFormattersFloat) / sizeof(cmsFormattersFloat); i++) {
            const cmsFormattersFloat* f = OutputFormattersFloat + i;

            if ((dwInput & ~f ->Mask) == f ->Type) {
                fr.FmtFloat = f ->Frm;
                return fr;
            }
        }
        }
        break;

    default:;

    }

    fr.Fmt16 = NULL;
    return fr;
}


typedef struct _cms_formatters_factory_list {

    cmsFormatterFactory Factory;
    struct _cms_formatters_factory_list *Next;

} cmsFormattersFactoryList;

_cmsFormattersPluginChunkType _cmsFormattersPluginChunk = { NULL };


// Duplicates the zone of memory used by the plug-in in the new context
static
void DupFormatterFactoryList(struct _cmsContext_struct* ctx,
                                               const struct _cmsContext_struct* src)
{
   _cmsFormattersPluginChunkType newHead = { NULL };
   cmsFormattersFactoryList*  entry;
   cmsFormattersFactoryList*  Anterior = NULL;
   _cmsFormattersPluginChunkType* head = (_cmsFormattersPluginChunkType*) src->chunks[FormattersPlugin];

     _cmsAssert(head != NULL);

   // Walk the list copying all nodes
   for (entry = head->FactoryList;
       entry != NULL;
       entry = entry ->Next) {

           cmsFormattersFactoryList *newEntry = ( cmsFormattersFactoryList *) _cmsSubAllocDup(ctx ->MemPool, entry, sizeof(cmsFormattersFactoryList));

           if (newEntry == NULL)
               return;

           // We want to keep the linked list order, so this is a little bit tricky
           newEntry -> Next = NULL;
           if (Anterior)
               Anterior -> Next = newEntry;

           Anterior = newEntry;

           if (newHead.FactoryList == NULL)
               newHead.FactoryList = newEntry;
   }

   ctx ->chunks[FormattersPlugin] = _cmsSubAllocDup(ctx->MemPool, &newHead, sizeof(_cmsFormattersPluginChunkType));
}

// The interpolation plug-in memory chunk allocator/dup
void _cmsAllocFormattersPluginChunk(struct _cmsContext_struct* ctx,
                                    const struct _cmsContext_struct* src)
{
      _cmsAssert(ctx != NULL);

     if (src != NULL) {

         // Duplicate the LIST
         DupFormatterFactoryList(ctx, src);
     }
     else {
          static _cmsFormattersPluginChunkType FormattersPluginChunk = { NULL };
          ctx ->chunks[FormattersPlugin] = _cmsSubAllocDup(ctx ->MemPool, &FormattersPluginChunk, sizeof(_cmsFormattersPluginChunkType));
     }
}



// Formatters management
cmsBool  _cmsRegisterFormattersPlugin(cmsContext ContextID, cmsPluginBase* Data)
{
    _cmsFormattersPluginChunkType* ctx = ( _cmsFormattersPluginChunkType*) _cmsContextGetClientChunk(ContextID, FormattersPlugin);
    cmsPluginFormatters* Plugin = (cmsPluginFormatters*) Data;
    cmsFormattersFactoryList* fl ;

    // Reset to built-in defaults
    if (Data == NULL) {

          ctx ->FactoryList = NULL;
          return TRUE;
    }

    fl = (cmsFormattersFactoryList*) _cmsPluginMalloc(ContextID, sizeof(cmsFormattersFactoryList));
    if (fl == NULL) return FALSE;

    fl ->Factory    = Plugin ->FormattersFactory;

    fl ->Next = ctx -> FactoryList;
    ctx ->FactoryList = fl;

    return TRUE;
}

cmsFormatter CMSEXPORT _cmsGetFormatter(cmsContext ContextID,
                                        cmsUInt32Number Type,         // Specific type, i.e. TYPE_RGB_8
                                        cmsFormatterDirection Dir,
                                        cmsUInt32Number dwFlags)
{
    _cmsFormattersPluginChunkType* ctx = ( _cmsFormattersPluginChunkType*) _cmsContextGetClientChunk(ContextID, FormattersPlugin);
    cmsFormattersFactoryList* f;

    if (T_CHANNELS(Type) == 0) {
        static const cmsFormatter nullFormatter = { 0 };
        return nullFormatter;
    }

    for (f =ctx->FactoryList; f != NULL; f = f ->Next) {

        cmsFormatter fn = f ->Factory(ContextID, Type, Dir, dwFlags);
        if (fn.Fmt16 != NULL) return fn;
    }

    // Revert to default
    if (Dir == cmsFormatterInput)
        return _cmsGetStockInputFormatter(Type, dwFlags);
    else
        return _cmsGetStockOutputFormatter(Type, dwFlags);
}


// Return whatever given formatter refers to float values
cmsBool  _cmsFormatterIsFloat(cmsUInt32Number Type)
{
    return T_FLOAT(Type) ? TRUE : FALSE;
}

// Return whatever given formatter refers to 8 bits
cmsBool  _cmsFormatterIs8bit(cmsUInt32Number Type)
{
    cmsUInt32Number Bytes = T_BYTES(Type);

    return (Bytes == 1);
}

// Build a suitable formatter for the colorspace of this profile
cmsUInt32Number CMSEXPORT cmsFormatterForColorspaceOfProfile(cmsContext ContextID, cmsHPROFILE hProfile, cmsUInt32Number nBytes, cmsBool lIsFloat)
{

    cmsColorSpaceSignature ColorSpace      = cmsGetColorSpace(ContextID, hProfile);
    cmsUInt32Number        ColorSpaceBits  = (cmsUInt32Number) _cmsLCMScolorSpace(ContextID, ColorSpace);
    cmsInt32Number         nOutputChans    = cmsChannelsOfColorSpace(ContextID, ColorSpace);
    cmsUInt32Number        Float           = lIsFloat ? 1U : 0;

    // Unsupported color space?
    if (nOutputChans < 0) return 0;

    // Create a fake formatter for result
    return FLOAT_SH(Float) | COLORSPACE_SH(ColorSpaceBits) | BYTES_SH(nBytes) | CHANNELS_SH(nOutputChans);
}

// Build a suitable formatter for the colorspace of this profile
cmsUInt32Number CMSEXPORT cmsFormatterForPCSOfProfile(cmsContext ContextID, cmsHPROFILE hProfile, cmsUInt32Number nBytes, cmsBool lIsFloat)
{

    cmsColorSpaceSignature ColorSpace = cmsGetPCS(ContextID, hProfile);

    cmsUInt32Number ColorSpaceBits = (cmsUInt32Number) _cmsLCMScolorSpace(ContextID, ColorSpace);
    cmsUInt32Number nOutputChans = cmsChannelsOfColorSpace(ContextID, ColorSpace);
    cmsUInt32Number Float = lIsFloat ? 1U : 0;

    // Unsupported color space?
    if (nOutputChans < 0) return 0;

    // Create a fake formatter for result
    return FLOAT_SH(Float) | COLORSPACE_SH(ColorSpaceBits) | BYTES_SH(nBytes) | CHANNELS_SH(nOutputChans);
}
