/*
 * signalGenerator.h
 *
 *  Created on: 28 jun. 2020
 *      Author: Ildefonso Aspera
 */

#ifndef SIGNALGENERATOR_H_
#define SIGNALGENERATOR_H_


#define SIGNAL_GENERATOR_VERSION	"0.3.0"

typedef enum {
	SIG_COLOR_BLACK,
	SIG_COLOR_RED,
	SIG_COLOR_GREEN,
	SIG_COLOR_BLUE,
	SIG_COLOR_YELLOW,
	SIG_COLOR_CYAN,
	SIG_COLOR_MAGENTA,
	SIG_COLOR_WHITE,
	SIG_COLOR_CNT
} sigCol_t;

typedef enum {
	SIG_PAT_SQR,
	SIG_PAT_TRI,
	SIG_PAT_SINE,
	SIG_PAT_RAMP,
	SIG_PAT_CNT,
} sigPat_t;

typedef struct {
	uint8_t  *buffer;
	uint16_t upperLimit;	// Value for 100% PWM
	uint32_t nElem;			// Limited to 24 bits
	sigCol_t color;			// Keyword for intensity of each channel
	sigPat_t pattern;
	uint8_t  nChannels;		// 1 for colors with one component, 2 min for colors with two and 3 for RGB colors
	uint8_t  duty;			// For square signal, duty cycle. 0-100
	uint8_t  intensity;		// Scaler from 0 to 100 to scale color intensity
	size_t   sampleSize;
	bool     useLogScale;	// Compensate for brightness, so that 50% is half brightness of 100% (use log scale)
} sigCfg_t;

typedef enum {
	SIG_ERR_SUCCESS,
	SIG_ERR_INVALID_PARAMS,
	SIG_ERR_UNKNOWN,
} sigErr_t;

sigErr_t sig_genPattern(sigCfg_t *sigCfg);


#endif /* SIGNALGENERATOR_H_ */
