/*
** FFT and FHT routines
**  Copyright 1988, 1993; Ron Mayer
**  
**  fht(fz,n);
**      Does a hartley transform of "n" points in the array "fz".
**      
** NOTE: This routine uses at least 2 patented algorithms, and may be
**       under the restrictions of a bunch of different organizations.
**       Although I wrote it completely myself; it is kind of a derivative
**       of a routine I once authored and released under the GPL, so it
**       may fall under the free software foundation's restrictions;
**       it was worked on as a Stanford Univ project, so they claim
**       some rights to it; it was further optimized at work here, so
**       I think this company claims parts of it.  The patents are
**       held by R. Bracewell (the FHT algorithm) and O. Buneman (the
**       trig generator), both at Stanford Univ.
**       If it were up to me, I'd say go do whatever you want with it;
**       but it would be polite to give credit to the following people
**       if you use this anywhere:
**           Euler     - probable inventor of the fourier transform.
**           Gauss     - probable inventor of the FFT.
**           Hartley   - probable inventor of the hartley transform.
**           Buneman   - for a really cool trig generator
**           Mayer(me) - for authoring this particular version and
**                       including all the optimizations in one package.
**       Thanks,
**       Ron Mayer; mayer@acuson.com
** and added some optimization by
**           Mather    - idea of using lookup table
**           Takehiro  - some dirty hack for speed up
*/

#include <math.h>
#include "util.h"
#include "psymodel.h"
#include "globalflags.h"
#include "lame.h"

#define TRI_SIZE (5-1) /* 1024 =  4**5 */
static FLOAT costab[TRI_SIZE*2];
static FLOAT window[BLKSIZE], window_s[BLKSIZE_s], scalefac;

static void fht(FLOAT *fz, int n)
{
    int i,k1,k2,k3,k4;
    FLOAT *fi, *fn, *gi;
    FLOAT *tri;

    fn = fi = fz + n;
    do {
	FLOAT f0,f1,f2,f3;
	fi -= 4;
	f1    = fi[0]-fi[1];
	f0    = fi[0]+fi[1];
	f3    = fi[2]-fi[3];
	f2    = fi[2]+fi[3];
	fi[2] = (f0-f2);
	fi[0] = (f0+f2);
	fi[3] = (f1-f3);
	fi[1] = (f1+f3);
    } while (fi != fz);

    tri = &costab[0];
    k1 = 1;
    do {
	FLOAT s1, c1;
	int kx;
	k1  *= 4;
	k2  = k1 << 1;
	kx  = k1 >> 1;
	k4  = k2 << 1;
	k3  = k2 + k1;
	fi  = fz;
	gi  = fi + kx;
	do {
	    FLOAT f0,f1,f2,f3;
	    f1      = fi[0]  - fi[k1];
	    f0      = fi[0]  + fi[k1];
	    f3      = fi[k2] - fi[k3];
	    f2      = fi[k2] + fi[k3];
	    fi[k2]  = f0     - f2;
	    fi[0 ]  = f0     + f2;
	    fi[k3]  = f1     - f3;
	    fi[k1]  = f1     + f3;
	    f1      = gi[0]  - gi[k1];
	    f0      = gi[0]  + gi[k1];
	    f3      = SQRT2  * gi[k3];
	    f2      = SQRT2  * gi[k2];
	    gi[k2]  = f0     - f2;
	    gi[0 ]  = f0     + f2;
	    gi[k3]  = f1     - f3;
	    gi[k1]  = f1     + f3;
	    gi     += k4;
	    fi     += k4;
	} while (fi<fn);
	c1 = tri[0];
	s1 = tri[1];
	for (i = 1; i < kx; i++) {
	    FLOAT c2,s2;
	    c2 = 1.0 - 2.0*s1*s1;
	    s2 = 2.0*s1*c1;
	    fi = fz + i;
	    gi = fz + k1 - i;
	    do {
		FLOAT a,b,g0,f0,f1,g1,f2,g2,f3,g3;
		b       = s2*fi[k1] - c2*gi[k1];
		a       = c2*fi[k1] + s2*gi[k1];
		f1      = fi[0 ]    - a;
		f0      = fi[0 ]    + a;
		g1      = gi[0 ]    - b;
		g0      = gi[0 ]    + b;
		b       = s2*fi[k3] - c2*gi[k3];
		a       = c2*fi[k3] + s2*gi[k3];
		f3      = fi[k2]    - a;
		f2      = fi[k2]    + a;
		g3      = gi[k2]    - b;
		g2      = gi[k2]    + b;
		b       = s1*f2     - c1*g3;
		a       = c1*f2     + s1*g3;
		fi[k2]  = f0        - a;
		fi[0 ]  = f0        + a;
		gi[k3]  = g1        - b;
		gi[k1]  = g1        + b;
		b       = c1*g2     - s1*f3;
		a       = s1*g2     + c1*f3;
		gi[k2]  = g0        - a;
		gi[0 ]  = g0        + a;
		fi[k3]  = f1        - b;
		fi[k1]  = f1        + b;
		gi     += k4;
		fi     += k4;
	    } while (fi<fn);
	    c2 = c1;
	    c1 = c2 * tri[0] - s1 * tri[1];
	    s1 = c2 * tri[1] + s1 * tri[0];
        }
	tri += 2;
    } while (k4<n);
}

static const int rv_tbl[] = {
    0x00,0x200,0x100,0x300,    0x80,0x280,0x180,0x380,
    0x40,0x240,0x140,0x340,    0xc0,0x2c0,0x1c0,0x3c0,
    0x20,0x220,0x120,0x320,    0xa0,0x2a0,0x1a0,0x3a0,
    0x60,0x260,0x160,0x360,    0xe0,0x2e0,0x1e0,0x3e0,
    0x10,0x210,0x110,0x310,    0x90,0x290,0x190,0x390,
    0x50,0x250,0x150,0x350,    0xd0,0x2d0,0x1d0,0x3d0,
    0x30,0x230,0x130,0x330,    0xb0,0x2b0,0x1b0,0x3b0,
    0x70,0x270,0x170,0x370,    0xf0,0x2f0,0x1f0,0x3f0,
    0x08,0x208,0x108,0x308,    0x88,0x288,0x188,0x388,
    0x48,0x248,0x148,0x348,    0xc8,0x2c8,0x1c8,0x3c8,
    0x28,0x228,0x128,0x328,    0xa8,0x2a8,0x1a8,0x3a8,
    0x68,0x268,0x168,0x368,    0xe8,0x2e8,0x1e8,0x3e8,
    0x18,0x218,0x118,0x318,    0x98,0x298,0x198,0x398,
    0x58,0x258,0x158,0x358,    0xd8,0x2d8,0x1d8,0x3d8,
    0x38,0x238,0x138,0x338,    0xb8,0x2b8,0x1b8,0x3b8,
    0x78,0x278,0x178,0x378,    0xf8,0x2f8,0x1f8,0x3f8,
    0x04,0x204,0x104,0x304,    0x84,0x284,0x184,0x384,
    0x44,0x244,0x144,0x344,    0xc4,0x2c4,0x1c4,0x3c4,
    0x24,0x224,0x124,0x324,    0xa4,0x2a4,0x1a4,0x3a4,
    0x64,0x264,0x164,0x364,    0xe4,0x2e4,0x1e4,0x3e4,
    0x14,0x214,0x114,0x314,    0x94,0x294,0x194,0x394,
    0x54,0x254,0x154,0x354,    0xd4,0x2d4,0x1d4,0x3d4,
    0x34,0x234,0x134,0x334,    0xb4,0x2b4,0x1b4,0x3b4,
    0x74,0x274,0x174,0x374,    0xf4,0x2f4,0x1f4,0x3f4,
    0x0c,0x20c,0x10c,0x30c,    0x8c,0x28c,0x18c,0x38c,
    0x4c,0x24c,0x14c,0x34c,    0xcc,0x2cc,0x1cc,0x3cc,
    0x2c,0x22c,0x12c,0x32c,    0xac,0x2ac,0x1ac,0x3ac,
    0x6c,0x26c,0x16c,0x36c,    0xec,0x2ec,0x1ec,0x3ec,
    0x1c,0x21c,0x11c,0x31c,    0x9c,0x29c,0x19c,0x39c,
    0x5c,0x25c,0x15c,0x35c,    0xdc,0x2dc,0x1dc,0x3dc,
    0x3c,0x23c,0x13c,0x33c,    0xbc,0x2bc,0x1bc,0x3bc,
    0x7c,0x27c,0x17c,0x37c,    0xfc,0x2fc,0x1fc,0x3fc,
    0x02,0x202,0x102,0x302,    0x82,0x282,0x182,0x382,
    0x42,0x242,0x142,0x342,    0xc2,0x2c2,0x1c2,0x3c2,
    0x22,0x222,0x122,0x322,    0xa2,0x2a2,0x1a2,0x3a2,
    0x62,0x262,0x162,0x362,    0xe2,0x2e2,0x1e2,0x3e2,
    0x12,0x212,0x112,0x312,    0x92,0x292,0x192,0x392,
    0x52,0x252,0x152,0x352,    0xd2,0x2d2,0x1d2,0x3d2,
    0x32,0x232,0x132,0x332,    0xb2,0x2b2,0x1b2,0x3b2,
    0x72,0x272,0x172,0x372,    0xf2,0x2f2,0x1f2,0x3f2,
    0x0a,0x20a,0x10a,0x30a,    0x8a,0x28a,0x18a,0x38a,
    0x4a,0x24a,0x14a,0x34a,    0xca,0x2ca,0x1ca,0x3ca,
    0x2a,0x22a,0x12a,0x32a,    0xaa,0x2aa,0x1aa,0x3aa,
    0x6a,0x26a,0x16a,0x36a,    0xea,0x2ea,0x1ea,0x3ea,
    0x1a,0x21a,0x11a,0x31a,    0x9a,0x29a,0x19a,0x39a,
    0x5a,0x25a,0x15a,0x35a,    0xda,0x2da,0x1da,0x3da,
    0x3a,0x23a,0x13a,0x33a,    0xba,0x2ba,0x1ba,0x3ba,
    0x7a,0x27a,0x17a,0x37a,    0xfa,0x2fa,0x1fa,0x3fa,
    0x06,0x206,0x106,0x306,    0x86,0x286,0x186,0x386,
    0x46,0x246,0x146,0x346,    0xc6,0x2c6,0x1c6,0x3c6,
    0x26,0x226,0x126,0x326,    0xa6,0x2a6,0x1a6,0x3a6,
    0x66,0x266,0x166,0x366,    0xe6,0x2e6,0x1e6,0x3e6,
    0x16,0x216,0x116,0x316,    0x96,0x296,0x196,0x396,
    0x56,0x256,0x156,0x356,    0xd6,0x2d6,0x1d6,0x3d6,
    0x36,0x236,0x136,0x336,    0xb6,0x2b6,0x1b6,0x3b6,
    0x76,0x276,0x176,0x376,    0xf6,0x2f6,0x1f6,0x3f6,
    0x0e,0x20e,0x10e,0x30e,    0x8e,0x28e,0x18e,0x38e,
    0x4e,0x24e,0x14e,0x34e,    0xce,0x2ce,0x1ce,0x3ce,
    0x2e,0x22e,0x12e,0x32e,    0xae,0x2ae,0x1ae,0x3ae,
    0x6e,0x26e,0x16e,0x36e,    0xee,0x2ee,0x1ee,0x3ee,
    0x1e,0x21e,0x11e,0x31e,    0x9e,0x29e,0x19e,0x39e,
    0x5e,0x25e,0x15e,0x35e,    0xde,0x2de,0x1de,0x3de,
    0x3e,0x23e,0x13e,0x33e,    0xbe,0x2be,0x1be,0x3be,
    0x7e,0x27e,0x17e,0x37e,    0xfe,0x2fe,0x1fe,0x3fe
};






void fft_short(
    FLOAT *x_real, FLOAT energy[3][HBLKSIZE_s], int chn, short *buffer[2])
{
    int i, j, b;

    if (chn < 2) {
	for (j = 0; j < BLKSIZE_s / 2; j++) {
	    FLOAT w = window_s[j];
	    for (b = 0; b < 3; b++) {
		int k = (576 / 3) * (b + 1);
		int g = BLKSIZE_s * b;
		x_real[rv_tbl[4 * j] + g] = w * buffer[chn][j + k];
		x_real[BLKSIZE_s - rv_tbl[4 * j] + g - 1]
		    = w * buffer[chn][BLKSIZE_s - j + k - 1];
	    }
	}
    } else if (chn == 2) {
	for (j = 0; j < BLKSIZE_s / 2; j++) {
	    FLOAT w = window_s[j] * scalefac;
	    for (b = 0; b < 3; b++) {
		int k = (576 / 3) * (b + 1);
		int g = BLKSIZE_s * b;
		x_real[rv_tbl[4 * j] + g] =
		    w * (buffer[0][j + k] + buffer[1][j + k]);
		x_real[BLKSIZE_s - rv_tbl[4 * j] + g - 1] =
		    w * (buffer[0][BLKSIZE_s - j + k - 1] + buffer[1][BLKSIZE_s - j + k - 1]);
	    }
	}
    } else {
	for (j = 0; j < BLKSIZE_s / 2; j++) {
	    FLOAT w = window_s[j] * scalefac;
	    for (b = 0; b < 3; b++) {
		int k = (576 / 3) * (b + 1);
		int g = BLKSIZE_s * b;
		x_real[rv_tbl[4 * j] + g] =
		    w * (buffer[0][j + k] - buffer[1][j + k]);
		x_real[BLKSIZE_s - rv_tbl[4 * j] + g - 1] =
		    w * (buffer[0][BLKSIZE_s - j + k - 1] - buffer[1][BLKSIZE_s - j + k - 1]);
	    }
	}
    }

    for (b = 0; b < 3; b++) {
	int g = BLKSIZE_s * b;
	fht(&x_real[g], BLKSIZE_s);


	energy[b][0] = x_real[g] * x_real[g];
	i = 1; j = BLKSIZE_s - 1;
	do {
	    FLOAT re, im;
	    re = x_real[i + g];
	    im = x_real[j + g];
	    energy[b][i] = (re * re + im * im) * 0.5;
	    i++; j--;
	} while (i != j);
	energy[b][i] = x_real[i + g] * x_real[i + g];
    }
}

void fft_long(
    FLOAT *x_real, FLOAT energy[HBLKSIZE], int chn, short *buffer[2])
{
    int i,j;

    if (chn < 2) {
	for (j = 0; j < BLKSIZE / 2; j++) {
	    FLOAT w = window[j];
	    x_real[rv_tbl[j]] = w * buffer[chn][j];
	    x_real[BLKSIZE - rv_tbl[j] - 1] = w * buffer[chn][BLKSIZE - j - 1];
	}
    } else if (chn == 2) {
	for (j = 0; j < BLKSIZE / 2; j++) {
	    FLOAT w = window[j] * scalefac;
	    x_real[rv_tbl[j]] = w * (buffer[0][j] + buffer[1][j]);
	    x_real[BLKSIZE - rv_tbl[j] - 1] =
		w * (buffer[0][BLKSIZE - j - 1] + buffer[1][BLKSIZE - j - 1]);
	}
    } else {
	for (j = 0; j < BLKSIZE / 2; j++) {
	    FLOAT w = window[j] * scalefac;
	    x_real[rv_tbl[j]] = w * (buffer[0][j] - buffer[1][j]);
	    x_real[BLKSIZE - rv_tbl[j] - 1] =
		w * (buffer[0][BLKSIZE - j - 1] - buffer[1][BLKSIZE - j - 1]);
	}
    }

    fht(x_real, BLKSIZE);

    energy[0] = x_real[0] * x_real[0];
    i = 1; j = BLKSIZE - 1;
    do {
	FLOAT a,b;
	a = x_real[i];
	b = x_real[j];
	energy[i] = (a * a + b * b) * 0.5;
	i++; j--;
    } while (j != i);
    energy[i] = x_real[i] * x_real[i];
}


void init_fft(void)
{
    int i;

    FLOAT r = PI*0.125;
    for (i = 0; i < TRI_SIZE; i++) {
	costab[i*2  ] = cos(r);
	costab[i*2+1] = sin(r);
	r *= 0.25;
    }

    scalefac = 1/SQRT2;
    /*
     * calculate HANN window coefficients 
     */
    for (i = 0; i < BLKSIZE / 2; i++)
	window[i] = 0.5 * (1.0 - cos(2.0 * PI * (i + 0.5) / BLKSIZE));
    for (i = 0; i < BLKSIZE_s / 2; i++)
	window_s[i] = 0.5 * (1.0 - cos(2.0 * PI * (i + 0.5) / BLKSIZE_s));
}









