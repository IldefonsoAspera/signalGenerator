#include <stdint.h>
#include <stdbool.h>
#include <string.h> 


// User selectable configuration
#define SAMPLE_SIZE		1		// Size of each resulting signal sample, in bytes



// Not user selectable configuration

#if !(SAMPLE_SIZE == 1		\
	||SAMPLE_SIZE == 2		\
	||SAMPLE_SIZE == 4)	 
#error "invalid sample size"
#endif


#define CONV_US_2_S			1000UL
#define CONV_100s_Hz_2_Hz	100UL


// Created with formula: X = (2^((n+1)/32) - 1) * 2^16
// Needs to be interpolated to turn into a 256 values array and then divided by 2^16 
// to enter in the range 0-255, if needed
// Formula: val[i] = logScale[i/8] + (logScale[(i/8)+1] - logScale[i/8])*(i%8)/8
static const uint16_t logScale[33] = {
	0,    55,   114,   184,   267,   366,   484,   624, 
	790,   988,  1224,  1504,  1837,  2233,  2704,  3264, 
	3930,  4722,  5663,  6783,  8115,  9699, 11583, 13823, 
	16487, 19655, 23422, 27902, 33230, 39565, 47100, 56060, 
	65535
};


// Lookup table for sine. Just a quarter is needed to make the whole set of values
// Formula for interpolating from 32 values to 256
// val[i] = sineQuarter[i/8] + (sineQuarter[(i/8)+1] - sineQuarter[i/8])*(i%8)/8
static const uint16_t sineQuarter[33] = {
	0, 3228, 6449, 9653, 12835, 15985, 19096, 22161, 
	25172, 28122, 31004, 33811, 36535, 39171, 41711, 44151, 
	46483, 48702, 50803, 52781, 54630, 56347, 57927, 59366,
	60662, 61810, 62808, 63653, 64344, 64878, 65255, 65474, 
	65535
};


typedef enum {
	LED_COLOR_BLACK,
	LED_COLOR_RED,
	LED_COLOR_GREEN,
	LED_COLOR_BLUE,
	LED_COLOR_YELLOW,
	LED_COLOR_CYAN,
	LED_COLOR_MAGENTA,
	LED_COLOR_WHITE,
	LED_COLOR_CNT
} ledCol_t;


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


typedef enum {
	LED_PAT_SQR,
	LED_PAT_TRI,
	LED_PAT_SINE,
	LED_PAT_RAMP,
	LED_PAT_CNT,
} ledPat_t;


typedef struct {
	uint8_t  *buffer;
	uint32_t upperLimit;	// Value for 100% PWM
	uint32_t nElem; 
	ledCol_t color;
	ledPat_t pattern;
	uint8_t  nChannels;		// 1 for colors with one component, 2 min for colors with two and 3 for RGB colors
	uint8_t  duty;			// For square signal, duty cycle. 0-100
	uint8_t  intensity;		// Scaler from 0 to 100 to scale color intensity
	bool     useLogScale;	// Compensate for brightness, so that 50% is half brightness of 100% (use log scale)
} ledCfg_t;


typedef enum {
	LED_ERR_SUCCESS,
	LED_ERR_INVALID_PARAMS,
	LED_ERR_UNKNOWN,
} ledErr_t;


/*****************************************
            Helper functions
******************************************/


static inline void insertSample(uint8_t *ptr, uint32_t data)
{
	*ptr = data&0xFF;
#if SAMPLE_SIZE > 1
	*(ptr+1) = (data>>8)&0xFF;
#if SAMPLE_SIZE > 2
	*(ptr+2) = (data>>16)&0xFF;
	*(ptr+3) = (data>>24)&0xFF;
#endif
#endif
}


/*****************************************
        end Helper functions
******************************************/

typedef void (*led_getColors_t)(uint32_t, ledCfg_t, uint32_t);

// Gets the color components from the palette, which is 8b, and 
// scales them to peakVal, so that UINT8_MAX is converted to peakVal
static void led_getColorsLin(uint32_t *colors, ledCfg_t *ledCfg, uint32_t peakVal)
{
	uint8_t i;
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = ((uint64_t)peakVal * palette[ledCfg->color].ch[i]) / UINT8_MAX;
}



static void led_getColorsLog(uint32_t *colors, ledCfg_t *ledCfg, uint32_t peakVal)
{
	uint8_t i;
	uint8_t idx;

	for(i=0; i<ledCfg->nChannels; i++)
	{
		idx = palette[ledCfg->color].ch[i];		// Get color components from palette
		// Convert to log and scale to peakVal
		colors[i] = logScale[idx/8] + (logScale[(idx/8)+1] - logScale[idx/8])*(idx%8)/8;
		colors[i] = ((uint64_t)peakVal * colors[i]) / UINT8_MAX;
	}
}


static void led_genPatSqr(ledCfg_t *ledCfg)
{
	uint32_t peakVal   = ((uint64_t)ledCfg->upperLimit * ledCfg->intensity) / 100UL;
	uint32_t nElemHigh = ((uint64_t)ledCfg->nElem * ledCfg->duty) / 100UL;
	uint32_t nElemLow  = ledCfg->nElem - nElemHigh;
	uint32_t colors[3];
	uint8_t  i;

	if(ledCfg->useLogScale)
		led_getColorsLog(colors, ledCfg, peakVal);
	else
		led_getColorsLin(colors, ledCfg, peakVal);

	for(; nElemHigh>0; nElemHigh--)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			insertSample(ledCfg->buffer, colors[i]);
			ledCfg->buffer += SAMPLE_SIZE;
		}
	}
	memset(ledCfg->buffer, 0, nElemLow * ledCfg->nChannels * SAMPLE_SIZE);
}


// TODO compensar si hubiera que hacer curva logaritmica
static void led_genPatRamp(ledCfg_t *ledCfg)
{
	uint32_t peakVal = ((uint64_t)ledCfg->upperLimit * ledCfg->intensity) / 100UL;
	uint32_t value;
	uint32_t colors[3];
	uint32_t i, j, temp;

	// Get final color components for ramp
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = palette[ledCfg->color].ch[i];

	// Build ramp with colors scaled linearly or in a logarithmic way
	for(j=0; j<ledCfg->nElem; j++)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint64_t)colors[i] * j)/ledCfg->nElem;
			if(ledCfg->useLogScale)
			{
				value = logScale[value/8] + (logScale[(value/8)+1] - logScale[value/8])*(value%8)/8;
				value = ((uint64_t)peakVal * value) / 65535;
			}else{
				value = ((uint64_t)peakVal * value) / 255;
			}

			insertSample(ledCfg->buffer, value);
			ledCfg->buffer += SAMPLE_SIZE;
		}
	}
}



static void led_genPatTri(ledCfg_t *ledCfg)
{
	uint32_t peakVal  = ((uint64_t)ledCfg->upperLimit * ledCfg->intensity) / 100UL;
	uint32_t value;
	uint32_t colors[3];
	uint32_t i, j;

	// Get final color components for ramp
	for(i=0; i<ledCfg->nChannels; i++)
		colors[i] = palette[ledCfg->color].ch[i];

	for(j=0; j<ledCfg->nElem/2; j++)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint64_t)colors[i] * j)/(ledCfg->nElem / 2);
			if(ledCfg->useLogScale)
			{
				value = logScale[value/8] + (logScale[(value/8)+1] - logScale[value/8])*(value%8)/8;
				value = ((uint64_t)peakVal * value) / 65535;
			}else{
				value = ((uint64_t)peakVal * value) / 255;
			}
			insertSample(ledCfg->buffer, value);
			ledCfg->buffer += SAMPLE_SIZE;
		}
	}

	for(; j>0; j--)
	{
		for(i=0;i<ledCfg->nChannels;i++)
		{
			value = ((uint64_t)colors[i] * j)/(ledCfg->nElem / 2);
			if(ledCfg->useLogScale)
			{
				value = logScale[value/8] + (logScale[(value/8)+1] - logScale[value/8])*(value%8)/8;
				value = ((uint64_t)peakVal * value) / 65535;
			}else{
				value = ((uint64_t)peakVal * value) / 255;
			}

			insertSample(ledCfg->buffer, value);
			ledCfg->buffer += SAMPLE_SIZE;
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
		|| ledCfg->nChannels <  palette[ledCfg->pattern].nChReq
		|| ledCfg->nChannels >  3
		|| (!(ledCfg->pattern == LED_PAT_SQR) 
			|| ledCfg->duty  >  100)
		|| ledCfg->intensity >  100)
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
		LED_ERR_UNKNOWN;
	}
}