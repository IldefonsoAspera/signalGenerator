#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "signalGenerator.h"

#define ARRAY_LENGTH(x)	(sizeof(x))/(sizeof(x[0]))

// TODO hacer union en duty para que se reutilice para invertir las señales

// Created with formula: X = 2^((n+1)/32 + 8) - 2^(1/32 + 8)*(255-n)/255
// Needs to be interpolated to turn into a 256 values array and then divided by 2^16
// to enter in the range 0-255, if needed
// Formula: val[i] = logScale[i/8] + (logScale[(i/8)+1] - logScale[i/8])*(i%8)/8
static const uint16_t logScale[33] = {
		0, 58, 125, 203, 294, 402, 528, 676,
		850, 1057, 1300, 1589, 1930, 2334, 2813, 3381,
		4055, 4856, 5806, 6934, 8274, 9866, 11758, 14006,
		16678, 19854, 23630, 28118, 33454, 39798, 47340, 56309,
		UINT16_MAX
};


// Alternative curve with slower increase on the high levels and faster on the lows
// Formula: X = 2^((n+1)/32 + 12) - 2^(1/32 + 12)*(255-n)/255
/*static const uint16_t logScale[33] = {
		0, 505, 1043, 1619, 2235, 2895, 3602, 4363,
		5180, 6059, 7006, 8028, 9130, 10319, 11605, 12996,
		14500, 16129, 17894, 19806, 21880, 24130, 26572, 29223,
		32102, 35230, 38629, 42324, 46342, 50712, 55466, 60638,
		65535
};*/


// Lookup table for sine. Just a quarter is needed to make the whole set of values
// Formula for interpolating from 32 values to 256
// val[i] = sineQuarter[i/8] + (sineQuarter[(i/8)+1] - sineQuarter[i/8])*(i%8)/8
static const uint16_t sineQuarter[33] = {
		0, 3228, 6449, 9653, 12835, 15985, 19096, 22161,
		25172, 28122, 31004, 33811, 36535, 39171, 41711, 44151,
		46483, 48702, 50803, 52781, 54630, 56347, 57927, 59366,
		60662, 61810, 62808, 63653, 64344, 64878, 65255, 65474,
		UINT16_MAX
};




typedef struct {
	uint8_t nChReq;
	uint8_t ch[3];
}color_t;


// If user wants to use 1 color, red channel will be used
// For 2 colors, channels red and green will be used
// Color array is organized as {R,G,B}. Position 0
// indicates minimum number of channels required for color
const color_t palette[SIG_COLOR_CNT] = {
		{1, {0, 0, 0}},
		{1, {255, 0, 0}},
		{2, {0, 255, 0}},
		{3, {0, 0, 255}},

		{2, {255, 255, 0}},
		{3, {0, 255, 255}},
		{3, {255, 0, 255}},
		{3, {255, 255, 255}},
};



/*****************************************
            Helper functions
 ******************************************/
//typedef void (*sig_getColors_t)(uint32_t, sigCfg_t, uint32_t);

static inline void insertSample(uint8_t **p_ptr, uint16_t data, size_t nBytes)
{
	uint8_t *ptr = *p_ptr;

	switch(nBytes){
	case 1:
		*ptr = data&0xFF;
		break;
	case 2:
		*((uint16_t*)ptr) = data&0xFFFF;
		break;
	default:
		return;
	}
	*p_ptr += nBytes;
}


// Gets the color components from the palette, which is 8b, and
// scales them to peakVal, so that UINT8_MAX is converted to peakVal
static void sig_getColorsLin(uint16_t *colors, sigCfg_t *sigCfg, uint16_t peakVal)
{
	uint8_t i;
	for(i=0; i<sigCfg->nChannels; i++)
		colors[i] = ((uint32_t)peakVal * palette[sigCfg->color].ch[i]) / UINT8_MAX;
}


static inline uint16_t getLogConv(uint8_t color)
{
	uint16_t result;

	switch(color){
	case 0:
		result = 0;
		break;
	case UINT8_MAX:
		result = UINT16_MAX;
		break;
	default:
		// Convert to log and scale to peakVal
		result = logScale[color/8] + (uint32_t)(logScale[(color/8)+1] - logScale[color/8])*(color%8)/8;
		break;
	}
	return result;
}


static void sig_getColorsLog(uint16_t *colors, sigCfg_t *sigCfg, uint16_t peakVal)
{
	uint8_t i;

	for(i=0; i<sigCfg->nChannels; i++)
	{
		colors[i] = getLogConv(palette[sigCfg->color].ch[i]); // Get color components from palette
		colors[i] = ((uint32_t)peakVal * colors[i]) / UINT16_MAX;
	}
}

/*****************************************
        end Helper functions
 ******************************************/


static void sig_genPatSqr(sigCfg_t *sigCfg)
{
	uint16_t peakVal   = ((uint32_t)sigCfg->upperLimit * sigCfg->intensity) / 100U;
	uint32_t nElemHigh = ((uint32_t)sigCfg->nElem * sigCfg->duty) / 100UL;
	uint32_t nElemLow  = sigCfg->nElem - nElemHigh;
	uint16_t colors[3];
	uint8_t  i;

	if(sigCfg->useLogScale)
		sig_getColorsLog(colors, sigCfg, peakVal);
	else
		sig_getColorsLin(colors, sigCfg, peakVal);

	for(; nElemHigh>0; nElemHigh--)
		for(i=0;i<sigCfg->nChannels;i++)
			insertSample(&sigCfg->buffer, colors[i], sigCfg->sampleSize);
	memset(sigCfg->buffer, 0, nElemLow * sigCfg->nChannels * sigCfg->sampleSize);
}


static void sig_genPatRamp(sigCfg_t *sigCfg)
{
	uint16_t peakVal = ((uint32_t)sigCfg->upperLimit * sigCfg->intensity) / 100UL;
	uint16_t value;
	uint8_t colors[3];
	uint32_t i, j;

	// Get final color components for ramp
	for(i=0; i<sigCfg->nChannels; i++)
		colors[i] = palette[sigCfg->color].ch[i];

	// Build ramp with colors scaled linearly or in a logarithmic way
	for(j=0; j<sigCfg->nElem; j++)
	{
		for(i=0;i<sigCfg->nChannels;i++)
		{
			value = ((uint32_t)colors[i] * j)/sigCfg->nElem;
			if(sigCfg->useLogScale)
			{
				value = getLogConv(value);
				value = ((uint32_t)peakVal * value) / UINT16_MAX;
			}
			else
			{
				value = ((uint32_t)peakVal * value) / UINT8_MAX;
			}
			insertSample(&sigCfg->buffer, value, sigCfg->sampleSize);
		}
	}
}


static void sig_genPatTri(sigCfg_t *sigCfg)
{
	uint16_t peakVal = ((uint32_t)sigCfg->upperLimit * sigCfg->intensity) / 100UL;
	uint16_t value;
	uint8_t colors[3];
	uint32_t i, j, k;

	// Get final color components for ramp
	for(i=0; i<sigCfg->nChannels; i++)
		colors[i] = palette[sigCfg->color].ch[i];

	for(k=0;k<2;k++)
	{
		for(j=0; j<sigCfg->nElem/2; j++)
		{
			for(i=0;i<sigCfg->nChannels;i++)
			{
				value = ((uint32_t)colors[i] * j)/(sigCfg->nElem / 2);
				if(k==1) value = UINT8_MAX - value;		// Invert if in second half

				if(sigCfg->useLogScale)
				{
					value = getLogConv(value);
					value = ((uint32_t)peakVal * value) / UINT16_MAX;
				}else{
					value = ((uint32_t)peakVal * value) / UINT8_MAX;
				}
				insertSample(&sigCfg->buffer, value, sigCfg->sampleSize);
			}
		}

	}
}


static void sig_genPatSine(sigCfg_t *sigCfg)
{
	uint16_t peakVal  = ((uint32_t)sigCfg->upperLimit * sigCfg->intensity) / 100UL;
	uint16_t value;
	uint8_t idx;
	uint8_t colors[3];
	uint32_t i, j, k;

	for(i=0; i<sigCfg->nChannels; i++)
		colors[i] = palette[sigCfg->color].ch[i];

	for(k=0; k<4; k++)							// Iterations for each quarter of the sinusoidal
	{
		for(j=0; j<sigCfg->nElem/4; j++)		// Iterations for each element of the quarter
		{
			for(i=0;i<sigCfg->nChannels;i++)	// For each of the channels in a sample
			{
				// Get color from the palette scaled by the sine curve
				idx = UINT8_MAX * j / (sigCfg->nElem/4);
				idx = (k & 0x01) ? UINT8_MAX - idx : idx;
				value = sineQuarter[idx/8] + (sineQuarter[(idx/8)+1] - sineQuarter[idx/8])*(idx%8)/8;
				value = (k > 1) ? (UINT16_MAX/2 + 1) - value/2 : (UINT16_MAX/2 + 1) + value/2;
				value = ((uint32_t)colors[i] * value) / UINT8_MAX;

				if(sigCfg->useLogScale)
				{
					value = getLogConv(value>>8);
					value = ((uint32_t)peakVal * value) / UINT16_MAX;
				}else{
					value = ((uint32_t)peakVal * value) / UINT16_MAX;
				}
				insertSample(&sigCfg->buffer, value, sigCfg->sampleSize);
			}
		}
	}
}


sigErr_t sig_genPattern(sigCfg_t *sigCfg)
{
	if(sigCfg->buffer    == NULL
			|| sigCfg->color     >= SIG_COLOR_CNT
			|| sigCfg->pattern   >= SIG_PAT_CNT
			|| sigCfg->nChannels == 0
			|| sigCfg->nChannels <  palette[sigCfg->color].nChReq
			|| sigCfg->nChannels >  3
			|| (sigCfg->pattern == SIG_PAT_SQR && sigCfg->duty > 100)
			|| sigCfg->intensity >  100
			|| (sigCfg->sampleSize != 1 && sigCfg->sampleSize != 2)
			|| sigCfg->nElem >= (1<<24))
		return SIG_ERR_INVALID_PARAMS;


	switch(sigCfg->pattern){
	case SIG_PAT_SQR:
		sig_genPatSqr(sigCfg);
		break;
	case SIG_PAT_RAMP:
		sig_genPatRamp(sigCfg);
		break;
	case SIG_PAT_SINE:
		sig_genPatSine(sigCfg);
		break;
	case SIG_PAT_TRI:
		sig_genPatTri(sigCfg);
		break;
	default:
		return SIG_ERR_UNKNOWN;
	}

	return SIG_ERR_SUCCESS;
}
