/*
 * signalGenerator.h
 *
 *  Created on: 28 jun. 2020
 *      Author: Ildefonso Aspera
 */

#ifndef SIGNALGENERATOR_H_
#define SIGNALGENERATOR_H_


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

typedef enum {
	LED_PAT_SQR,
	LED_PAT_TRI,
	LED_PAT_SINE,
	LED_PAT_RAMP,
	LED_PAT_CNT,
} ledPat_t;

typedef struct {
	uint8_t  *buffer;
	uint16_t upperLimit;	// Value for 100% PWM
	uint32_t nElem;			// Limited to 24 bits
	ledCol_t color;
	ledPat_t pattern;
	uint8_t  nChannels;		// 1 for colors with one component, 2 min for colors with two and 3 for RGB colors
	uint8_t  duty;			// For square signal, duty cycle. 0-100
	uint8_t  intensity;		// Scaler from 0 to 100 to scale color intensity
	size_t   sampleSize;
	bool     useLogScale;	// Compensate for brightness, so that 50% is half brightness of 100% (use log scale)
} ledCfg_t;

typedef enum {
	LED_ERR_SUCCESS,
	LED_ERR_INVALID_PARAMS,
	LED_ERR_UNKNOWN,
} ledErr_t;

ledErr_t led_genPattern(ledCfg_t *ledCfg);


#endif /* SIGNALGENERATOR_H_ */
