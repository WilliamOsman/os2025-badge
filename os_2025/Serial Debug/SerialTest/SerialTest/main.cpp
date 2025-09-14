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


int main(void)
{
	initTXPin();
    /* Replace with your application code */
    while (1) 
    {
		Serial.println("hello");
		_delay_ms(1000);
    }
}

