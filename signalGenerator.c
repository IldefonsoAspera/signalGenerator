#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "signalGenerator.h"

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
const color_t palette[LED_COLOR_CNT] = {
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
//typedef void (*led_getColors_t)(uint32_t, ledCfg_t, uint32_t);

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
static void led_getColorsLin(uint16_t *colors, ledCfg_t *ledCfg, uint16_t peakVal)
{
	uint8_t i;
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = ((uint32_t)peakVal * palette[ledCfg->color].ch[i]) / UINT8_MAX;
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


static void led_getColorsLog(uint16_t *colors, ledCfg_t *ledCfg, uint16_t peakVal)
{
	uint8_t i;

	for(i=0; i<ledCfg->nChannels; i++)
	{
		colors[i] = getLogConv(palette[ledCfg->color].ch[i]); // Get color components from palette
		colors[i] = ((uint32_t)peakVal * colors[i]) / UINT16_MAX;
	}
}

/*****************************************
        end Helper functions
 ******************************************/


static void led_genPatSqr(ledCfg_t *ledCfg)
{
	uint16_t peakVal   = ((uint32_t)ledCfg->upperLimit * ledCfg->intensity) / 100U;
	uint32_t nElemHigh = ((uint32_t)ledCfg->nElem * ledCfg->duty) / 100UL;
	uint32_t nElemLow  = ledCfg->nElem - nElemHigh;
	uint16_t colors[3];
	uint8_t  i;

	if(ledCfg->useLogScale)
		led_getColorsLog(colors, ledCfg, peakVal);
	else
		led_getColorsLin(colors, ledCfg, peakVal);

	for(; nElemHigh>0; nElemHigh--)
		for(i=0;i<ledCfg->nChannels;i++)
			insertSample(&ledCfg->buffer, colors[i], ledCfg->sampleSize);
	memset(ledCfg->buffer, 0, nElemLow * ledCfg->nChannels * ledCfg->sampleSize);
}


static void led_genPatRamp(ledCfg_t *ledCfg)
{
	uint16_t peakVal = ((uint32_t)ledCfg->upperLimit * ledCfg->intensity) / 100UL;
	uint16_t value;
	uint8_t colors[3];
	uint32_t i, j;

	// Get final color components for ramp
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = palette[ledCfg->color].ch[i];

	// Build ramp with colors scaled linearly or in a logarithmic way
	for(j=0; j<ledCfg->nElem; j++)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint32_t)colors[i] * j)/ledCfg->nElem;
			if(ledCfg->useLogScale)
			{
				value = getLogConv(value);
				value = ((uint32_t)peakVal * value) / UINT16_MAX;

			}else
			{
				value = ((uint32_t)peakVal * value) / UINT8_MAX;
			}

			insertSample(&ledCfg->buffer, value, ledCfg->sampleSize);
		}
	}
}


static void led_genPatTri(ledCfg_t *ledCfg)
{
	uint16_t peakVal  = ((uint32_t)ledCfg->upperLimit * ledCfg->intensity) / 100UL;
	uint16_t value;
	uint8_t colors[3];
	uint32_t i, j;

	// Get final color components for ramp
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = palette[ledCfg->color].ch[i];

	for(j=0; j<ledCfg->nElem/2; j++)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint32_t)colors[i] * j)/(ledCfg->nElem / 2);
			if(ledCfg->useLogScale)
			{
				value = getLogConv(value);
				value = ((uint32_t)peakVal * value) / UINT16_MAX;
			}else{
				value = ((uint32_t)peakVal * value) / UINT8_MAX;
			}
			insertSample(&ledCfg->buffer, value, ledCfg->sampleSize);
		}
	}

	for(; j>0; j--)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint32_t)colors[i] * j)/(ledCfg->nElem / 2);
			if(ledCfg->useLogScale)
			{
				value = getLogConv(value);
				value = ((uint32_t)peakVal * value) / UINT16_MAX;
			}else{
				value = ((uint32_t)peakVal * value) / UINT8_MAX;
			}
			insertSample(&ledCfg->buffer, value, ledCfg->sampleSize);
		}
	}
}


static void led_genPatSine(ledCfg_t *ledCfg)
{

	//val[i] = sineQuarter[i/8] + (sineQuarter[(i/8)+1] - sineQuarter[i/8])*(i%8)/8
}


ledErr_t led_genPattern(ledCfg_t *ledCfg)
{
	if(ledCfg->buffer    == NULL
			|| ledCfg->color     >= LED_COLOR_CNT
			|| ledCfg->pattern   >= LED_PAT_CNT
			|| ledCfg->nChannels == 0
			|| ledCfg->nChannels <  palette[ledCfg->color].nChReq
			|| ledCfg->nChannels >  3
			|| (ledCfg->pattern == LED_PAT_SQR && ledCfg->duty > 100)
			|| ledCfg->intensity >  100
			|| (ledCfg->sampleSize != 1 && ledCfg->sampleSize != 2)
			|| ledCfg->nElem >= (1<<24))
		return LED_ERR_INVALID_PARAMS;


	switch(ledCfg->pattern){
	case LED_PAT_SQR:
		led_genPatSqr(ledCfg);
		break;
	case LED_PAT_RAMP:
		led_genPatRamp(ledCfg);
		break;
	case LED_PAT_SINE:
		led_genPatSine(ledCfg);
		break;
	case LED_PAT_TRI:
		led_genPatTri(ledCfg);
		break;
	default:
		return LED_ERR_UNKNOWN;
	}

	return LED_ERR_SUCCESS;
}
