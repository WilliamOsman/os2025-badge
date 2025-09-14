/*
 * SerialTest.cpp
 *
 * Created: 9/13/2025 2:53:17 PM
 * Author : CumMaker
 */ 
#define F_CPU	8000000

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/wdt.h>
#include "ATtinySerialOut.hpp"

#define NUM_LEDS	4
#define LED1_OFFSET		(1 << 0)
#define LED2_OFFSET		(1 << 1)
#define LED3_OFFSET		(1 << 2)
#define LED4_OFFSET		(1 << 3)
//#define LED5_OFFSET		(1 << 4)
#define LED1_PORT	PORTB
#define LED2_PORT	PORTB
#define LED3_PORT	PORTB
#define LED4_PORT	PORTB
//#define LED5_PORT	PORTB

void init_leds(void);
void set_led(uint8_t led_num, uint8_t state);

void all_off(void);

//----------LED UTILITY---------------	

inline void led_on(uint8_t led_num){
	switch(led_num){
		case 0x0:
			LED1_PORT &= ~(LED1_OFFSET);
			break;
		case 0x1:
			LED2_PORT &= ~(LED2_OFFSET);
			break;
		case 0x2:
			LED3_PORT &= ~(LED3_OFFSET);
			break;
		case 0x3:
			LED4_PORT &= ~(LED4_OFFSET);
			break;
		case 0x4:
			//LED5_PORT &= ~(LED5_OFFSET);
			break;
	}
	//*((uint8_t*)pgm_read_word_near(LED_PORTS + led_num)) =  *((uint8_t*)pgm_read_word_near(LED_PORTS + led_num)) | LED_OFFSETS[led_num];
}


inline void led_off(uint8_t led_num){
	switch(led_num){
		case 0x0:
			LED1_PORT |= (LED1_OFFSET);
			break;
		case 0x1:
			LED2_PORT |= (LED2_OFFSET);
			break;
		case 0x2:
			LED3_PORT |= (LED3_OFFSET);
			break;
		case 0x3:
			LED4_PORT |= (LED4_OFFSET);
			break;
		case 0x4:
			//LED5_PORT |= (LED5_OFFSET);
			break;
	}
}

inline void all_off(void){
	for(uint8_t i = 0; i < NUM_LEDS; i++){
		led_off(i);
	}
}

inline void all_on(void){
	for(uint8_t i = 0; i < NUM_LEDS; i++){
		led_on(i);
	}
}

void test_leds(void){
	all_on();
	_delay_us(10000);
	all_off();
}

void init_leds(void){
	// bit of an ugly way to do this, but it allows for various port configs

	DDRB |= LED1_OFFSET;
	DDRB |= LED2_OFFSET;
	DDRB |= LED3_OFFSET;
	DDRB |= LED4_OFFSET;
	//DDRB |= LED5_OFFSET;

	all_off();
}	



int main(void)
{
	// set main clock prescaler to 1 for highest cpu speed
	CLKPR = (1<<CLKPCE);
	CLKPR = 0;
	
	initTXPin();
	init_leds();
	
    /* Replace with your application code */
    while (1) 
    {
		led_on(0);
		Serial.println("hello");
		_delay_ms(500);
		led_off(0);
		Serial.print(100);
		Serial.println();
		_delay_ms(500);
    }
}

