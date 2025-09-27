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

// -----Optical PROGRAMMING------
#define PHOTO_RATE_HZ	10		//doesn't do anything, rate is set by tick comparison in timer0 ISR
#define OVERSAMPLE	8			//changing won't affect oversample rate, only used as reference
#define SAMPLE_HISTORY	8		//number of bits to store subsample min/max data for exposure tracking
#define CHARGE_TICKS	2		//100us ticks to keep pull-up pin engaged
#define INTEGRATION_COUNT	32	//number of samples per ADC integration (power of 2)
#define SIGNAL_STRENGTH_FACTOR 20	//(0 to 100) Higher value requires more contrast between bright/dark
#define START_FLAG	0b1110		//bits indicating transfer start

#define FRAME_WIDTH 6
#define MAX_FRAMES 24
#define FRAME_CYCLES 0			// number of shake cycles to display each frame

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

#define PHOTODIODE_PORT PORTB
#define PHOTODIODE_OFFSET (1 << 5)
#define PHOTODIODE_PIN PINB
#define PHOTODIODE_ADC_CH 0

volatile uint8_t tim_cnt_low = 0;
volatile uint8_t tim_cnt_high = 0;
volatile uint8_t tick = 0;



#endif

void run(void);
void init_leds(void);
void init(void);
void set_led(uint8_t led_num, uint8_t state);
void all_off(void);

// stuff for user programming
void init_adc(void);
uint16_t sample_adc(void);
bool user_program(void);
void load_frames(void);

void animate2(void);

uint8_t data_buf[FRAME_WIDTH*MAX_FRAMES] = {0};	// frame buffer
uint8_t data_frame_count = 0;
uint8_t default_data_size = 5;	// number of frames

//OPEN SAUCE
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

//SAUCE
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
volatile uint16_t ADC_shorted_cycles = 100;
volatile uint8_t run_mode = 0;
const uint8_t clock_offset = 1;	//tick needs slight auto-adjustment for inaccurate programming app fps
volatile uint16_t tick_time = 0;

	
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
	//for(uint8_t i = 0; i < 5; i++){
	if(run_mode != 2) return;
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
	//}
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
	tick_time++;
	
	//if(delay_ticks <= 1) delay_ticks--;
	//else delay_ticks = 0;
	
	if (tick >= 125 + clock_offset) {              // t = 12.5 ms + slight offset for drift
		tick = 0;                                  // next cycle
	}
}


ISR(ADC_vect)
{
	static uint8_t count = 0;
	static uint16_t integrate = 0;
	
	
	uint16_t reading = ADC;
	
	//check shake resistor for short circuit
	//Run mode can be determined by duration of short/open circuit 
	if(count == 0){
		//state change to short circuit
		if(reading < 10 && !ADC_shorted){
			ADC_shorted = true;
			ADC_shorted_cycles = 1;
		}
		//state change to open circuit
		else if(reading >= 10 && ADC_shorted){
			ADC_shorted = false;
			ADC_shorted_cycles = 1;
		}		
		ADC_shorted_cycles++;
		if(ADC_shorted_cycles >= 1000) ADC_shorted_cycles = 1000;
		
		//if ADC hasn't been shorted recently run in program mode
		if(!ADC_shorted && ADC_shorted_cycles > 100) run_mode = 0;
		//if ADC is continuously shorted display error
		else if(ADC_shorted && ADC_shorted_cycles > 10) run_mode = 2;
		//if ADC shorted run in animation mode for
		else run_mode = 1;
		
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

inline void delayTicks(uint16_t durration){
	uint16_t start_time = tick_time;
	while(tick_time - start_time <= durration);
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

//---------- Running Modes ---------------	

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
	
	const uint8_t led_signal_steady = 1;
	const uint8_t led_state_now = 0;
	
	// ------------- Seek until successful transfer ------------------
	while(seekData)
	{
		static uint8_t stablewhen0 = 10;
		
		if(run_mode != 0) return 0; //exit programming mode early
		
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
			//OR
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
			
			//start flag detected
			if(stablewhen0 != 0){
				 rx = 0b0;  //clear RX
				 led_off(led_signal_steady);
			}
			else if ((!data_incoming) && (rx & (0b1111)) == START_FLAG ) 
			{
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
					//if 5th bit is 0, data is over, or read is corrupted
					if (!(rx & 0b1)) {
						//data read is finished, or data is corrupted
						data_incoming = false;
						seekData = false;						
						total_bytes = current_byte;
						
						// not a full frame detected
						if ((current_byte + 1) % FRAME_WIDTH != 0) {
							led_error(0);
							seekData = true;
						} 
						// didn't end on a full vertical line (minus the stop bit)
						else if (current_bit != 6) {
							led_error(0);
							seekData = true;
						} 
						// too many frames
						else if ((current_byte + 1) / FRAME_WIDTH > MAX_FRAMES) {
							led_error(0);
							seekData = true;
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
		}		
	}
	
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



uint8_t animate_left(uint8_t frame, uint16_t durration){
	
	uint16_t off_dur = durration >> 2;
	uint16_t on_dur = on_dur * 3;

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
		
		//_delay_us(1800);
		delayTicks(on_dur);
		all_off();
		delayTicks(off_dur);
		//_delay_us(500);
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
	
	while(1){
		if(run_mode != 1) return;
		// read bump sensor and adjust shake timing
		if(ADC_shorted) bump = 1;
		else bump = 0;

		if(bump & !last_bump){
			static uint16_t last_ticks = 0;
			
			uint16_t dur = (tick_time - last_ticks) >> 4;
			
			// rising edge
			_delay_ms(55);
			uint8_t ret = animate_left(frame_num, dur);
			if (ret >= data_frame_count)
				ret = 0;
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

void run(void){
	//setup ADC and ISR
	
	init_adc();
	timer0_tick_100us_init();
	sei();
	
	load_frames();
	
	while(1){
		all_off();
		
		//run_mode is determined in timer0_tick_100us_init ISR routine
		switch(run_mode){
			case 0:
				if(user_program()) load_frames();
				break;
			case 1:
				animate2();
				break;
			case 2:
				led_error(0);
				break;
			default:
				led_error(0);
				break;
		}
	}
}


void init(void){
	// set main clock prescaler to 1 for highest cpu speed
	CLKPR = (1<<CLKPCE);
	CLKPR = 0;
	
	init_leds();
}

int main(void)
{	
	_delay_ms(100/8);
	
	init();	
	
	test_leds();
	
	// handle quick power on/off to select a mode
	uint8_t mode = EEPROM_read(0xff);
	if(mode > 4) mode = 0;
	
	if(mode < 4) led_on(0);
	else led_on(4);
	
	if(mode+1 > 4){
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
			
			// erase EEPROM after 5 button presses
			case 4:	
				EEPROM_write(0, 0xff);	// just clear the frame size, no need to clear the entire frame memory
				mode = 0;
				for(uint8_t i = 0; i < 5; i++){
					led_on(4);
					_delay_ms(200);
					led_off(4);
					_delay_ms(200);
				}
				break;
			// normal mode
			default:
				mode = 0;
				run();
				break;
		}		
	}
}

