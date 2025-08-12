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
// range of accepted programming frequencies
#define PHOTO_HZ_MIN 5
#define PHOTO_HZ_MAX 60
#define PHOTO_RATE_HZ	10
#define OVERSAMPLE	8
#define PHOTO_RINGBUFF_LEN	128

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

ISR(TIMER0_OVF_vect) {
	// charge up port with pull-up
	PHOTODIODE_PORT |= PHOTODIODE_OFFSET;
	//set COMPA to somewhere around 800us
	//set COMPB to around 6ms (BEFORE OVERFLOW)
	
	// if start/end voltage isn't enough, we could track time for voltage to fall?
}

ISR(TIMER0_COMPA_vect){
	//stop charging port
	PHOTODIODE_PORT &= ~PHOTODIODE_OFFSET;
	// Start adc conversion
	//ADCSRA |= (1 << ADSC);
}
ISR(TIMER0_COMPB_vect){
	//start adc conversion
	ADCSRA |= (1 << ADSC);
}
ISR(ADC_vect){
	uint16_t result = ADC;  // ADC is a macro that does ADCL then ADCH
	rx_ring.buf[rx_ring.write_idx] = result; //load into buffer
	//increment buffer
	rx_ring.write_idx = (rx_ring.write_idx + 1) & rx_ring.mask;
}

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

//ring buffer to read user program data
typedef struct {
	uint16_t *buf;
	uint8_t   mask;  // cap - 1
	volatile uint8_t write_idx; // producer/head/end-of-data
	volatile uint8_t read_idx;  // consumer/tail/current read
} ring_t;

volatile uint16_t rx_storage[PHOTO_RINGBUFF_LEN] = {0};	//raw photodiode buffer

static ring_t rx_ring = {
	.buf = rx_storage,
	.mask = PHOTO_RINGBUFF_LEN - 1,
	.write_idx = 0,
	.read_idx = 0
};

uint8_t data_buf[FRAME_WIDTH*MAX_FRAMES] = {0};	// frame buffer
uint8_t data_frame_count = 0;

uint8_t default_data_size = 10;	// number of frames
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

void init_bumpers(void){
	// Set up the bumpers with pull ups
	BUMP_PORT |= (BUMP_OFFSET);
}

void test_leds(void){
	all_on();
	_delay_us(10000);
	all_off();
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

void init_timer(void){
	
	// 256 prescaler set to get 8.192 microsecond overflow
	#ifdef ATTINY84
		TCCR1B |= (1<<CS12 | 1<<CS10);
		TCNT1 = 0;
	#endif
	#ifdef ATTINY85
		TCCR0B = (1<<CS02) | (0<<CS01) |(0<<CS00);
		TCNT0 = 0;
		TIMSK  = (1<<OCIE0A) | (1<<OCIE0B) | (1<<TOIE0); // Enable compare A/B and overflow interrupts
		sei();
	#endif
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
	ADCSRA = (1<<ADEN) | (1<<ADIE) |
	| (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);
}

bool user_program(void){


	//------- STEP 1 --------- setup ISRs
	 
	
	//STEP 3 - process ring buffer
	//			- track floor/ceiling
	//			- track edges
	//			- estimate bit centers
	//STEP 4 - store into data array
	
	bool readData = true;
	
	while(readData){
		
		
		//------- STEP 2 --------- store in ring buffer
		
		//write new data to ring buffer
		rx_ring.buf[rx_ring.write_idx] = sample - sample_adc();
		//advance the write index by 1
		rx_ring.write_idx = (rx_ring.write_idx + 1) & rx_ring.mask;
		
		//------- STEP 3 --------- process ring buffer
		
		
			
		bool rising_edge = current_state & ~last_state;
		bool falling_edge  = ~current_state & last_state;
		last_state = current_state;
		
		uint16_t edge_time = get_timer();
		
		if (auto_adjust){
			if(current_state){	// led 0 always just shows the detected color
				led_on(0);
			}
			else{
				led_off(0);
			}
		}
		
		
		// attempt to sync to the clk
		if(valid_clk_cnt < 10 && rising_edge){
			set_timer(0);	// reset timer for next edge
			if(edge_time < min_us128 || edge_time > max_us128*2){
				valid_clk_cnt = 0;
				continue;
			}
			uint16_t edge_variance = edge_time > clk_period_us128 ? edge_time-clk_period_us128 : clk_period_us128-edge_time;
			clk_period_us128 = (clk_period_us128/4) * 3 + edge_time/4;
			if(edge_variance > edge_time>>4){	// variance is over 1/16 of the expected time
				valid_clk_cnt = 0;
				continue;
			}
			valid_clk_cnt++;
			if(valid_clk_cnt >= 10){
				led_off(0);
				//led_on(1);	// signify sync, now we're ready for data
				auto_adjust = 0;	// no longer try to adapt to brightness
				clk_period_us128 = clk_period_us128/2;	// use half of the total period to get a single bit time
				//clk_period_us128 += clk_period_us128/16;
			}
			continue;
		}
		
		if(valid_clk_cnt < 10){
			continue;
		}
		
		if(edge_time > clk_period_us128*16){
			break;	// no recent valid edges
		}
		
		// decode the data
		
		
		// start condition is data being on for longer than 3 periods then a falling edge
		switch(data_started){
			case 0:	// wait for high pulse
			if(falling_edge){
				set_timer(0);	// reset timer
			}
			else if(current_state && edge_time > clk_period_us128*3){
				data_started = 1;
			}
			break;
			case 1:	// wait for falling edge
			if(falling_edge){
				set_timer(0);
				data_started = 2;
			}
			break;
			case 2:	// skip first bit to get into the actual data
			if(edge_time > clk_period_us128>>1){
				set_timer(0);
				data_started = 3;
				//led_on(2);
			}
			break;
		}
		if(data_started!=3) continue;
		
		// reset timer on all edges to half a bit period
		if(rising_edge || falling_edge){
			guess_centers = 0;
			set_timer(clk_period_us128/2);
			continue;
		}
		
		if(edge_time > clk_period_us128){	// center of a data bit, save data
			guess_centers++;
			set_timer(0);	// reset for next bit
			data_buf[current_byte] |= current_state<<current_bit;
			current_bit++;
			if(current_bit > 5){
				if(!current_state){
					// bit 5 should always be 1 for keeping clock sync
					// if its not, we had a failure somewhere or are complete, exit programming mode
					break;
				}
				current_bit = 0;
				current_byte++;
			}
		}
		
		if(guess_centers > 8){
			break;
		}
		
	}
	
	if(data_started != 3){
		return 0;
	}
	
	
	// validate data
	
	// check we got data to fill a whole number of frames
	
	if((current_byte + 1) % FRAME_WIDTH != 0){
		return 0;	// not a full frame detected
	}
	if(current_bit != 6){
		return 0;	// didn't end on a full vertical line (minus the stop bit)
	}
	if((current_byte+1) / FRAME_WIDTH > MAX_FRAMES){
		return 0;	// too many frames
	}
	
	// good enough, probably not worth trying to do any real data validation
	
	// copy to eeprom
	
	EEPROM_write(0, 0);	// 0x0 is used as the frame count, set to zero while writing
	
	for(uint8_t i = 0; i < current_byte+1; i++){
		EEPROM_write(i+1, data_buf[i]);
	}
	
	// now write the size
	EEPROM_write(0, (current_byte+1)/FRAME_WIDTH);
	
	//flash LED for success
	for(uint8_t i = 0; i<3; i++){
		led_on(2);
		_delay_ms(200);
		led_off(2);
		_delay_ms(200);
	}
	
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
	CLKPR = 0b10000000;
	CLKPR = 0;
	
	init_leds();
	//init_bumpers();
	init_adc();
	init_timer();
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
						led_on(0);
					}
					else{
						led_off(0);
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
				}
				test_leds();
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

