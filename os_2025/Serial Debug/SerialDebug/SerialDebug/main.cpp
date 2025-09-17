/*
 * os_2025.c
 *
 * Created: 2/17/2025 12:41:47 AM
 * Author : user
 */ 

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/wdt.h>
#include "ATtinySerialOut.hpp"

// -----Optical PROGRAMMING------
// range of accepted programming frequencies
//#define PHOTO_HZ_MIN 5
//#define PHOTO_HZ_MAX 60
#define PHOTO_RATE_HZ	10
#define OVERSAMPLE	8
#define SAMPLE_HISTORY	8		//number of bits to store subsample min/max data for exposure tracking
#define PHOTO_RINGBUFF_LEN	128
#define CHARGE_TICKS	2		//100us ticks to keep pull-up pin engaged
#define INTEGRATION_COUNT	32	//number of samples per ADC integration (power of 2)
#define INTEGRATION_SHIFT	5	//number of shifts to divide by SUBSAMPLE_COUNT
#define SIGNAL_STRENGTH_FACTOR 20	//(0 to 100) Higher value requires more contrast between bright/dark
#define START_FLAG	0b1110		//bits indicating transfer start

#define FRAME_WIDTH 6
#define MAX_FRAMES 12
#define FRAME_CYCLES 0	// number of shake cycles to display each frame

#define BUMP_FILTER 50

//#define DISPLAY_MODE_FULL	// all frames at once
//#define DISPLAY_MODE_FRAME	// one frame at a time
#define DISPLAY_MODE_WORD

//#define ATTINY84	// test board
#define ATTINY85	// final badges


// pin setups for the 84
#ifdef ATTINY84

#define NUM_LEDS	5
#define LED1_OFFSET		(1 << 2)
#define LED2_OFFSET		(1 << 3)
#define LED3_OFFSET		(1 << 4)
#define LED4_OFFSET		(1 << 5)
#define LED5_OFFSET		(1 << 6)
#define LED1_PORT	PORTA
#define LED2_PORT	PORTA
#define LED3_PORT	PORTA
#define LED4_PORT	PORTA
#define LED5_PORT	PORTA

#define BUMP_OFFSET		(1 << 2)
#define BUMP_PORT	PORTB
#define BUMP_PIN	PINB

#define PHOTODIODE_PORT PORTA
#define PHOTODIODE_OFFSET (1 << 1)
#define PHOTODIODE_PIN PINA
#define PHOTODIODE_ADC_CH 1

#endif

// pin setups for the 85
#ifdef ATTINY85

#define NUM_LEDS	5
#define LED1_OFFSET		(1 << 0)
#define LED2_OFFSET		(1 << 1)
#define LED3_OFFSET		(1 << 2)
#define LED4_OFFSET		(1 << 3)
#define LED5_OFFSET		(1 << 4)
#define LED1_PORT	PORTB
#define LED2_PORT	PORTB
#define LED3_PORT	PORTB
#define LED4_PORT	PORTB
#define LED5_PORT	PORTB

#define BUMP_OFFSET		(1 << 5)
#define BUMP_PORT	PORTB
#define BUMP_PIN	PINB

#define PHOTODIODE_PORT PORTB
#define PHOTODIODE_OFFSET (1 << 5)
#define PHOTODIODE_PIN PINB
#define PHOTODIODE_ADC_CH 0

volatile uint8_t tim_cnt_low = 0;
volatile uint8_t tim_cnt_high = 0;

volatile uint8_t tick = 0;



#endif

void init_leds(void);
void init(void);
void set_led(uint8_t led_num, uint8_t state);
void set_led_frame(uint8_t led_frame);
void animate(uint8_t* led_frames, uint8_t num_frames);
void all_off(void);

// stuff for user programming
void init_timer(void);
uint16_t timer1_get_us(void);
void init_adc(void);
uint16_t sample_adc(void);
bool user_program(void);
void load_frames(void);

void animate2(void);

// Software Serial
// incoming buffer
volatile char *inbuf[32];
// outgoing buffer
volatile char *outbuf[32];

uint8_t data_buf[FRAME_WIDTH*MAX_FRAMES] = {0};	// frame buffer
uint8_t data_frame_count = 0;

uint8_t default_data_size = 5;	// number of frames
/*
uint8_t default_data[10*FRAME_WIDTH] = {
	0b1110, 0b10001, 0b10001, 0b10001, 0b1110, 0b0,		// O
	0b0, 0b11111, 0b1001, 0b1001, 0b0110, 0b0,			// P
	0b0, 0b11111, 0b10101, 0b10101, 0b10001, 0b0,		// E
	0b11111, 0b10, 0b100, 0b1000, 0b11111, 0b0,			// N
	0, 0, 0, 0, 0, 0,
	0b10000, 0b10111, 0b10101, 0b11101, 0b1, 0b0,		// S
	0b0, 0b11110, 0b101, 0b101, 0b11110, 0b0,			// A
	0b1111, 0b11000, 0b10000, 0b11000, 0b1111, 0b0,		// U
	0b1110, 0b10001, 0b10001, 0b10001, 0b10001, 0b0,	// C
	0b0, 0b11111, 0b10101, 0b10101, 0b10001, 0b0,		// E
	};
*/

uint8_t default_data[10*FRAME_WIDTH] = {
	0b10000, 0b10111, 0b10101, 0b11101, 0b1, 0b0,		// S
	0b0, 0b11110, 0b101, 0b101, 0b11110, 0b0,			// A
	0b1111, 0b11000, 0b10000, 0b11000, 0b1111, 0b0,		// U
	0b1110, 0b10001, 0b10001, 0b10001, 0b10001, 0b0,	// C
	0b0, 0b11111, 0b10101, 0b10101, 0b10001, 0b0,		// E
};
volatile bool newSample_available = false;
volatile uint16_t newSample = 0;
volatile bool ADC_shorted = false;	//if first ADC reading is 0v, the 0ohm resistor is shorted
const uint8_t clock_offset = 1;	//tick needs slight auto-adjustment for inaccurate programming app fps
	
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
			LED5_PORT &= ~(LED5_OFFSET);
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
			LED5_PORT |= (LED5_OFFSET);
			break;
	}
	//*((uint8_t*)pgm_read_word_near(LED_PORTS + led_num)) = *((uint8_t*)pgm_read_word_near(LED_PORTS + led_num)) & ~(LED_OFFSETS[led_num]);
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

inline void led_error(uint8_t led_num){
	all_off();
	for(uint8_t i = 0; i < 5; i++){
		led_on(0);
		led_off(1);
		_delay_ms(100);
		led_off(0);
		led_on(1);
		_delay_ms(100);
		led_on(0);
		led_off(1);
		_delay_ms(100);
		led_off(0);
		led_off(1);
	}
}

inline void led_success(uint8_t led_num){
	all_off();
	for(uint8_t i = 0; i < 15; i++){
		led_on(led_num);
		_delay_ms(50);
		led_off(led_num);
		_delay_ms(50);

	}
}

void test_leds(void){
	all_on();
	_delay_us(10000);
	all_off();
}

void init_leds(void){
	// bit of an ugly way to do this, but it allows for various port configs
	#ifdef ATTINY84
		if(LED1_PORT == PORTA){
			DDRA |= LED1_OFFSET;
		}
		else{
			DDRB |= LED1_OFFSET;
		}
		if(LED2_PORT == PORTA){
			DDRA |= LED2_OFFSET;
		}
		else{
			DDRB |= LED2_OFFSET;
		}
		if(LED3_PORT == PORTA){
			DDRA |= LED3_OFFSET;
		}
		else{
			DDRB |= LED3_OFFSET;
		}
		if(LED4_PORT == PORTA){
			DDRA |= LED4_OFFSET;
		}
		else{
			DDRB |= LED4_OFFSET;
		}
		if(LED5_PORT == PORTA){
			DDRA |= LED5_OFFSET;
		}
		else{
			DDRB |= LED5_OFFSET;
		}
	#endif
	#ifdef ATTINY85
		DDRB |= LED1_OFFSET;
		DDRB |= LED2_OFFSET;
		DDRB |= LED3_OFFSET;
		DDRB |= LED4_OFFSET;
		DDRB |= LED5_OFFSET;
	#endif
	
	all_off();
}	

//----------PHOTODIODE DATA---------------	
	
void timer0_tick_100us_init(void) {
	// CTC, OCR0A = 99, prescaler = 8  => 8 MHz / 8 = 1 MHz (1 µs/tick). 100 µs per interrupt.
	TCCR0A = (1<<WGM01);       // CTC
	OCR0A  = 99;               // 100 counts -> 100 µs
	TCCR0B = (1<<CS01);        // prescaler 8
	TIMSK  |= (1<<OCIE0A);     // enable compare A interrupt
}

void timer0_tick_100us_disable(void) {
	// CTC, OCR0A = 99, prescaler = 8  => 8 MHz / 8 = 1 MHz (1 µs/tick). 100 µs per interrupt.
	TCCR0A = (1<<WGM01);       // CTC
	OCR0A  = 99;               // 100 counts -> 100 µs
	TCCR0B = (1<<CS01);        // prescaler 8
	TIMSK  &= ~(1<<OCIE0A);     // enable compare A interrupt
}

void init_adc(){
	
	// Enable ADC by clearing Power Reduction ADC bit
	PRR &= ~(1 << PRADC);

	// Select reference = AVcc, channel = ADCn (05)
	// REFS1:0 = 00 ? Vcc as ref
	// MUX[5:0] = channel
	ADMUX = (PHOTODIODE_ADC_CH); // ADC0ADC5

	// Data alignment: for 10-bit read, clear ADLAR (left adjust);
	ADMUX &= ~(1 << ADLAR);

	// Set prescaler and enable ADC:
	// ADPS[2:0]=111 ? ÷128 (62.5? kHz at 8 MHz); ADEN=1
	// slowest we can sample
	ADCSRA = (1<<ADEN) // enable ADC
			| (1<<ADIE) // ADC interrupt enable
			| (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0); // divided 128
}

uint16_t sample_adc(void) {
	// Start conversion
	ADCSRA |= (1 << ADSC);

	// Wait for conversion to complete (ADSC clears)
	while (ADCSRA & (1 << ADSC));

	// Read result
	// If ADLAR=0: must read ADCL first, then ADCH
	uint16_t result = ADC;  // ADC is a macro that does ADCL then ADCH
	return result;
}

ISR(TIMER0_COMPA_vect) {
	// 100 µs tick scheduler
	switch (tick) {
		case 0: // t = 0
			// charge up port with pull-up
			PHOTODIODE_PORT |= PHOTODIODE_OFFSET;
			break;  
		case CHARGE_TICKS: // t = 200us
			//stop charging port after ~200us
			PHOTODIODE_PORT &= ~PHOTODIODE_OFFSET;
			//start adc conversion
			ADCSRA |= (1<<ADSC);
			break;
		default:  break;
	}
	tick++;
	if (tick >= 125 + clock_offset) {              // t = 12.5 ms + slight offset for drift
		tick = 0;                                  // next cycle
	}
}


ISR(ADC_vect)
{
	static uint8_t count = 0;
	static uint16_t integrate = 0;
	
	uint16_t reading = ADC;
	
	//check that the 0ohm resistor isn't shorted on first sample
	if(count == 0){
		if(reading < 10) ADC_shorted = true;
		else ADC_shorted = false;
	}
	
	integrate += ADC;								// ADC is a macro that does ADCL then ADCH
	count++;										//increment integration step
	
	if (count < INTEGRATION_COUNT){
		ADCSRA |= (1<<ADSC);						//trigger new ADC reading
	}
	else {
		newSample_available = true;					//flag indicates new data ready
		//newSample = integrate >> INTEGRATION_SHIFT;	//divide by total samples for average
		newSample = integrate;
		integrate = 0;								//reset integration
		count = 0;									//reset count
	}
	
	
}

//----------EEPROM---------------	

void EEPROM_write(uint8_t ucAddress, uint8_t ucData)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEPE))
	;
	/* Set Programming mode */
	EECR = (0<<EEPM1)|(0>>EEPM0);
	/* Set up address and data registers */
	EEARL = ucAddress;
	EEDR = ucData;
	/* Write logical one to EEMPE */
	EECR |= (1<<EEMPE);
	/* Start eeprom write by setting EEPE */
	EECR |= (1<<EEPE);
}

uint8_t EEPROM_read(uint8_t ucAddress)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEPE))
	;
	/* Set up address register */
	EEARL = ucAddress;
	/* Start eeprom read by writing EERE */
	EECR |= (1<<EERE);
	/* Return data from data register */
	return EEDR;
}



//----------BUMPERS---------------	

void init_bumpers(void){
	// Set up the bumpers with pull ups
	BUMP_PORT |= (BUMP_OFFSET);
}

static inline int bump_hit(void){
	static uint8_t high_count = 0;
	static uint8_t low_count = 0;
	static bool bump_state = 0;
	if(BUMP_PIN & BUMP_OFFSET){
		low_count++;
		high_count = 0;
	}
	else{
		high_count++;
		low_count = 0;
	}
	if(high_count >= BUMP_FILTER) bump_state = 1;
	if(low_count >= BUMP_FILTER) bump_state = 0;
	return bump_state;
}

bool user_program(void){
	/* Allows users to upload custom pixels to the display using the OpenSauce web interface
	https://opensauce.com/badge-25/
	*/
	
	uint8_t total_bytes = 0;
	
	bool seekData = true;
	bool state_now = 0;
	bool state_prev = 0;
	bool signal_available = false;
	uint8_t rx = 0;						//incoming bitstream storage
	bool newbit_available = false;
	bool data_incoming = false;
	bool first_read = true;
	bool signal_stable = false;
	
	const uint8_t led_signal_steady = 1;
	const uint8_t led_state_now = 0;
	
	

	//------- STEP 1 --------- setup sampling ISRs
	init_adc();
	timer0_tick_100us_init();
	sei();

	// ------------- Seek until successful transfer ------------------
	while(seekData)
	{
		static uint8_t stablewhen0 = 10;
		
		//------------ Process ADC Sample -----------------
		if(newSample_available)
		{
			static float data_smooth = 0;
			static float data_max = 0;
			static float data_min = 0;
			static uint16_t max_bucket[SAMPLE_HISTORY];
			static uint16_t min_bucket[SAMPLE_HISTORY];
			static uint8_t bucket_sample = 0;
			static uint8_t bucket_writepos = 0;
				
			newSample_available = false;
			
			if(first_read){
				first_read = false;
				data_smooth = newSample;
				data_max = newSample;
				data_min = newSample;
				for(uint8_t block = 0; block < SAMPLE_HISTORY; block++){
					max_bucket[block] = newSample;
					min_bucket[block] = newSample;
				}
			}
			
			if(ADC_shorted) led_error(0);
			
			data_smooth = data_smooth * 0.5 + (float)newSample * 0.5;			
			
			// windowed historic maximum/minimum over a large number of samples without storing every sample
			// each bucket contains a max/min that represents 8 previous samples
			// old buckets are overwritten, so historic data generally follows the signal
			if(data_smooth > max_bucket[bucket_writepos]) max_bucket[bucket_writepos] = data_smooth;
			else if(data_smooth < min_bucket[bucket_writepos]) min_bucket[bucket_writepos] = data_smooth;
			
			bucket_sample++;
			
			if(bucket_sample >= OVERSAMPLE){
				bucket_sample = 0;
				bucket_writepos++;
				if(bucket_writepos >= SAMPLE_HISTORY) bucket_writepos = 0;
				max_bucket[bucket_writepos] = data_smooth;
				min_bucket[bucket_writepos] = data_smooth;
			}
			
			//lock the max/min values if data transfer is active
			if(!data_incoming)
			{
				data_max = max_bucket[0];
				data_min = min_bucket[0];
			
				for(uint8_t block = 1; block < SAMPLE_HISTORY; block++)
				{
					if(max_bucket[block] > data_max) data_max = max_bucket[block];
					else if(min_bucket[block] < data_min) data_min = min_bucket[block];
				}
			}

			float amplitude = (data_max - data_min);
			float midline = amplitude / 2 + data_min;	//threshold between high and low
							
			//good signal if optical signal has large enough contrast
			//good signal if data transfer is active
			if( data_incoming || amplitude >= data_max * (SIGNAL_STRENGTH_FACTOR / 100.0) ) signal_available = true;
			else{
				led_off(led_signal_steady);
				stablewhen0 = 10;
				signal_available = false;
			}
			
			//convert analog signal to boolean
			if (data_smooth < midline) state_now = 1;  //invert the reading
			else state_now = 0;
			/*
			Serial.print(newSample);
			Serial.print(",");
			Serial.print(data_smooth);
			Serial.print(",");
			Serial.print(data_max);
			Serial.print(",");
			Serial.print(data_min);
			Serial.print(",");
			Serial.print(midline);
			Serial.print(",");
			Serial.print(amplitude);
			Serial.print(",");
			Serial.print(signal_available * 500);
			Serial.println();
			*/
				
		}
		
		//------------ Convert to Bitstream -----------------
		if (signal_available)
		{
			//calculate bit width
			static uint8_t period = 8;
			static uint8_t halfperiod = 4;
			static uint8_t bit_count = 0;
			static uint8_t width = 0;
			static uint8_t next_bit = 0;
			static uint8_t stable_counter = 0;
			
			signal_available = false;
			
			if(state_now) led_on(led_state_now);
			else led_off(led_state_now);

			//detect bit on edge
			if (state_now != state_prev) 
			{
				if(stablewhen0 != 0){
					stablewhen0--;
					led_off(led_signal_steady);
				}
				else led_on(led_signal_steady);
				
				//Serial.print("E");
				state_prev = state_now;
				//write the first bit
				rx = (rx << 1) | state_now;  //load next bit
				newbit_available = true;
				next_bit = period + halfperiod + 1;
				bit_count = 1;
				width = 1;
				stable_counter++;
			}

			//detect bit on center
			else if (width == next_bit) 
			{
				//Serial.print("C");
				rx = (rx << 1) | state_now;  //load next bit
				newbit_available = true;
				next_bit += period;
				bit_count++;
				stable_counter = 0;
			}
			width++;
		}
		
		//------------ Process Bitstream -----------------
		if (newbit_available) 
		{			
			newbit_available = false;
			//Serial.print(state_now);
			
			//start flag detected
			if(stablewhen0 != 0){
				 rx = 0b0;  //clear RX
				 led_off(led_signal_steady);
			}
			else if ((!data_incoming) && (rx & (0b1111)) == START_FLAG ) 
			{
				//Serial.print(":START");
				data_incoming = true;
				rx = 0b0;  //clear RX
				data_buf[0] = 0b0;
			}			

			else if (data_incoming) 
			{
				static uint8_t current_bit = 0;
				static uint8_t current_byte = 0;
				
				data_buf[current_byte] |= (state_now << current_bit);

				current_bit++;

				if (current_bit > 5) {
					//Serial.print(":BYTE");
					//if 5th bit is 0, data is over, or read is corrupted
					if (!(rx & 0b1)) {
						//Serial.println(":FINISH");
						//data read is finished, or data is corrupted
						data_incoming = false;
						seekData = false;						
						total_bytes = current_byte;
						
						// not a full frame detected
						if ((current_byte + 1) % FRAME_WIDTH != 0) {
							led_error(0);
							seekData = true;
							//return 0;	
						} 
						// didn't end on a full vertical line (minus the stop bit)
						else if (current_bit != 6) {
							led_error(0);
							seekData = true;
							//return 0;	
						} 
						// too many frames
						else if ((current_byte + 1) / FRAME_WIDTH > MAX_FRAMES) {
							led_error(0);
							seekData = true;
							//return 0;	
						}
						
						//reset for new data transmission
						current_byte = 0;
						current_bit = 0;
						
					}
					//finished reading column, move onto next byte
					else {
						current_byte++;
						current_bit = 0;
						data_buf[current_byte] = 0b0;
					}
				}
			}
			//Seriali.println();
		}		
	}
					
	timer0_tick_100us_disable();
	
	EEPROM_write(0, 0);	// 0x0 is used as the frame count, set to zero while writing
	
	for(uint8_t i = 0; i < total_bytes+1; i++){
		EEPROM_write(i+1, data_buf[i]);
	}
	
	// now write the size
	EEPROM_write(0, (total_bytes+1)/FRAME_WIDTH);
	
	//flash LED for success
	led_success(2);

	return 1;
}

void load_frames(void){
	// load eeprom or default data into the frame buffer for displaying
	data_frame_count = EEPROM_read(0x0);
	
	if(data_frame_count != 0xff && data_frame_count != 0x0){
		// eeprom data exists, use it
		for(uint8_t i = 0; i < data_frame_count*FRAME_WIDTH; i++){
			data_buf[i] = EEPROM_read(i+1);
		}
		return;
	}
	
	// no or bad eeprom, use default
	data_frame_count = default_data_size;
	for(uint8_t i = 0; i < data_frame_count*FRAME_WIDTH; i++){
		data_buf[i] = default_data[i];
	}
}

uint8_t animate_left(uint8_t frame){
	uint8_t total_cols = FRAME_WIDTH * data_frame_count;	
	
	#ifdef DISPLAY_MODE_FULL
		_delay_ms(20);
		// all frames at once
		uint8_t total_cols = FRAME_WIDTH * data_frame_count;
		
		for(uint8_t i = 0; i < total_cols; i++) {
			uint8_t index = total_cols-1-i;
			uint8_t column = data_buf[index];
			for(uint8_t l = 0; l < NUM_LEDS; l++) {
				if((column>>l) & 0b1){
					led_on(l);
				} else {
					led_off(l);
				}
			}
			
			_delay_us(1800);
			all_off();
			_delay_us(500);
			
		}
	#endif
	
#ifdef DISPLAY_MODE_WORD
	_delay_ms(20);
	// all frames up to a blank one
	
	uint8_t word_end = data_frame_count;
	for (uint8_t f=frame+1; f < data_frame_count; f++) {
		uint8_t blank_cnt = 0;
		//uint8_t *cur_frame = &(data_buf[f*FRAME_WIDTH]);
		for (uint8_t c = 0; c < FRAME_WIDTH; c++) {
			//if ((cur_frame[c] & 0x1f) == 0) {
			uint8_t line = data_buf[c + f*FRAME_WIDTH];
			if ((line & 0x1f) == 0) {
				blank_cnt++;
			} else {
				blank_cnt = 0;
			}
		}
		if (blank_cnt >= FRAME_WIDTH) {
			word_end = f;
			break;
		}
	}
	
	uint8_t col_min = frame*FRAME_WIDTH;
	uint8_t col_max = word_end*FRAME_WIDTH;
	for (uint8_t col = col_max; col > col_min; col--) {
		uint8_t column = data_buf[col-1];
		
		// write to LEDs
		for(uint8_t l = 0; l < NUM_LEDS; l++){
			if((column>>l) & 0b1){
				led_on(l);
			}
			else{
				led_off(l);
			}
		}
		
		_delay_us(1800);		
		all_off();
		_delay_us(500);
	}
	return word_end;
#endif

#ifdef DISPLAY_MODE_FRAME
		// one frame at a time
		
		_delay_ms(30);
		
		for(uint8_t i = 0; i < FRAME_WIDTH; i++) {
			uint8_t index = frame*FRAME_WIDTH + (FRAME_WIDTH-1-i);
			uint8_t column = data_buf[index];
			for(uint8_t l = 0; l < NUM_LEDS; l++) {
				if((column>>l) & 0b1) {
					led_on(l);
				} else {
					led_off(l);
				}
			}
			
			_delay_us(2000);
			
			all_off();
			
			_delay_us(800);
		}
#endif
	
	return 0;
}

void animate2(void){
	bool bump = 0;
	bool last_bump = 0;
	uint8_t frame_num = 0;
	
	uint8_t cycles = 0;
	
	//uint16_t consecutive_bump_detects = 0;
	
	uint8_t last_starting_col = FRAME_WIDTH * data_frame_count;
	
	while(1){
		
		// read bump sensor and adjust shake timing
		bump = bump_hit();
		/*
		if(bump){
			consecutive_bump_detects++;
		}
		else{
			consecutive_bump_detects = 0;
		}
		if(consecutive_bump_detects > 2000){
			all_on();
		}
		*/
		if(bump & !last_bump){
			// rising edge
			//consecutive_bump_detects = 0;
			_delay_ms(55);
			uint8_t ret = animate_left(frame_num);
			if (ret >= data_frame_count)
				ret = 0;
			//frame_num = ret;
			all_off();
			_delay_ms(80);
			cycles++;
			if(cycles > FRAME_CYCLES){
				cycles = 0;
				frame_num = ret;
			}
		}
		if(!bump & last_bump){
			// falling edge
		}
		last_bump = bump;
		
		_delay_us(10);
		
	}
}


void init(void){
	// set main clock prescaler to 1 for highest cpu speed
	CLKPR = (1<<CLKPCE);
	CLKPR = 0;
	
	init_leds();
	//init_bumpers();
	init_adc();
	//init_timer();
	//initTXPin();
}

int main(void)
{	
	_delay_ms(100/8);
	
	init();	
	
	test_leds();
	
	// handle quick power on/off to select a mode
	uint8_t mode = EEPROM_read(0xff);
	led_on(mode);
	
	if(mode+1 > 3){
		EEPROM_write(0xff, 0);
	}
	else{
		EEPROM_write(0xff, mode+1);
	}
	
	_delay_ms(1000);
	EEPROM_write(0xff, 0);
	
	all_off();
	
	//mode = 2;	// for testing
	
    while(1) 
    {
		switch(mode){
			case 0:	// normal animation mode
				load_frames();
				BUMP_PORT |= BUMP_OFFSET;	// enable bump sensor pull-up
				animate2();
				break;
			case 1:	// bump sensor alignment mode
				BUMP_PORT |= BUMP_OFFSET;	// enable bump sensor pull-up
				while(1){
					if(bump_hit()){
						led_on(1);
					}
					else{
						led_off(1);
					}
					_delay_us(10);
				}
				break;
			case 2:	// program mode
				if(user_program()){
					led_on(2);
					_delay_ms(200);
					led_off(2);
					mode = 0;
					//Serial.println(mode);
				}
				break;
			case 3:	// erase EEPROM
				for(uint8_t i = 0; i < 5; i++){
					led_on(3);
					_delay_ms(200);
					led_off(3);
					_delay_ms(200);
				}
				EEPROM_write(0, 0xff);	// just clear the frame size, no need to clear the entire frame memory
				mode = 0;
				break;
		}		
	}
}

