#pragma once

#include "stm32f4xx.h"

// Free-running timer 1.5 MHz
#define TIMER_1500_KHZ_VALUE		TIM10->CNT

// Restart and synchronize 1.5 us timer
#define TIMER_1500_KHZ_SYNC			TIM10->EGR = 1

#define SOFTUSB_ENABLE_IRQ			NVIC_EnableIRQ(TIM2_IRQn)
#define SOFTUSB_DISABLE_IRQ			NVIC_DisableIRQ(TIM2_IRQn)

// Fast set/reset of + and - pins
#define SOFTUSB_M				*_bssr = _m
#define SOFTUSB_P				*_bssr = _p
#define SOFTUSB_Z				*_bssr = _z
#define SOFTUSB_OUT(v)			*_bssr = v

// Toggle input/output
#define SOFTUSB_INPUT	\
	_gpio->MODER = (_gpio->MODER & (~(3ul << (_mpin * 2)))) | (0 << (_mpin * 2));	\
	_gpio->MODER = (_gpio->MODER & (~(3ul << (_ppin * 2)))) | (0 << (_ppin * 2))

#define SOFTUSB_OUTPUT	\
	_gpio->MODER = (_gpio->MODER & (~(3ul << (_mpin * 2)))) | (1 << (_mpin * 2));	\
	_gpio->MODER = (_gpio->MODER & (~(3ul << (_ppin * 2)))) | (1 << (_ppin * 2))

// Read macro
#define SOFTUSB_READ(v)	\
		v = _gpio->IDR
		
// Sync to 1.5 us macro
#define SOFTUSB_WAIT	\
		t = TIMER_1500_KHZ_VALUE;	\
		while (t == TIMER_1500_KHZ_VALUE)

// Save 1.5 us timer value
#define SOFTUSB_BEGIN_INTERVAL	\
		t = TIMER_1500_KHZ_VALUE

// Sync to a next timer's tick
#define SOFTUSB_WAIT_TICK	\
		while (t == TIMER_1500_KHZ_VALUE)

// Platform constructor part
#define SOFTUSB_PLATFORM_CTOR	\
		_gpio = (GPIO_TypeDef *)(((unsigned long)GPIOA) + _port * (((unsigned long)GPIOB) - ((unsigned long)GPIOA)));	\
		_m = (1 << _mpin) | (0x10000u << _ppin);	\
		_p = (1 << _ppin) | (0x10000u << _mpin);	\
		_z = (0x10000u << _mpin) | (0x10000u << _ppin);	\
		_bssr = (volatile unsigned int *)&_gpio->BSRRL

// Platform private fields
#define SOFTUSB_PLATFORM_PRIVATE\
	GPIO_TypeDef *_gpio;	\
	unsigned int _m;	\
	unsigned int _p;	\
	unsigned int _z;	\
	volatile unsigned int *_bssr
