/*
 * Task2.c
 *
 * Created: 21/10/2022 17:07:38
 * Author : Eleanor
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>

// Pre-compile mode selection if using the ADC & Potentiometer, comment out/remove for button control.
//#define USE_ADC

/*
Port register bits to Arduino pin numbers (in C binary number mapping order)
Port D  : 0  0  0  0  0  0  0  0
Arduino : 7  6  5  4  3  2  1  0
Port B  : 0  0  0  0  0  0  0  0
Arduino : -  -  13 12 11 10 9  8
Port C  : 0  0  0  0  0  0  0  0
Arduino : -  R  A5 A4 A3 A2 A1 A0
*/

// Define the binary value to set pin 6 only as output using port D
#define DDRD_PIN_6_OUT 0b01000000

// First two bits of TCCR0A determine compare match behavior for OC0A,
// values selected from datasheet for toggling on compare match (fast PWM)
#define TCCR0A_CLEAR_A 0b10000000 // Set at timer start, unset at compare match
// Set WGM01 and WGM00 for fast PWM, counting up to 0xFF
#define TCCR0A_FAST_PWM_FULL_RANGE 0b00000011
// TCCR0B bits to set for a 1x clock divider
#define TCCR0B_CLK_1x 0b00000001 
// Define EICRA bits to set for a falling edge interrupt on INT0
#define EICRA_FALLING_INT0 0x02
// Define EICRA bits to set for a falling edge interrupt on INT1
#define EICRA_FALLING_INT1 (0x02 << 2) // 0x08?
// Define port B output pins
#define PORTB_OUTPUTS 0b00001100
#define PORTB_MOTOR_FORWARD 0b00001000
#define PORTB_MOTOR_BACKWARD 0b00000100

// Flag to indicate a full button press has been registered to main loop (button 1).
volatile uint8_t button_pressed;
// Flag to indicate a full button press has been registered to main loop (button 2).
volatile uint8_t direction_change;
// Flag to indicate new duty cycle requested via potentiometer
volatile uint8_t dutycycle_change_adc;
// ISR-filled potentiometer value
volatile uint8_t new_adc_val;
// Track previous potentiometer value
uint8_t prev_adc_val;

// Store the duty cycle values to switch between
#define N_DUTY_CYCLE_VALUES 4
//const float DUTY_CYCLE_VALUES [N_DUTY_CYCLE_VALUES] = {0.0, 0.25, 0.625, 0.875};
const float DUTY_CYCLE_VALUES [N_DUTY_CYCLE_VALUES] = {0.4, 0.6, 0.8, 1.0};
	
// Program state variable to track next duty cycle output to set
uint8_t next_duty_cycle_idx;
// State variable for a boolean direction flag
uint8_t direction;

// External interrupt_zero ISR, updates flag requesting PWM stepped through.
ISR (INT0_vect) 
{
	// Add one to the button pressed flag (using addition/subtraction instead
	// of value setting to allow for queue type use)
	button_pressed = 1;
}

// External interrupt_one ISR, updates flag for motor direction.
ISR (INT1_vect)
{
	direction_change = 1;
}

ISR (ADC_vect)
{
	// Only record most significant 8 bits of value, equivalent to shifting right by two
	new_adc_val = ADCH;
	// If the sampled value is different from the last set, flag an update is required.
	if (new_adc_val != prev_adc_val) {
		dutycycle_change_adc = 1;
	}
}

void update_motor_direction()
{
	// Switch port B output to set/unset 1A/2A on H-bridge driver
	if (direction)
	{
		PORTB = PORTB_MOTOR_FORWARD;
	}
	else
	{
		PORTB = PORTB_MOTOR_BACKWARD;
	}
}

void set_pwm_dutycycle(float percentage)
{
	TCCR0B = 0x00; // Temporarily stop timer while updating parameters
	//TCNT0 = 0x00; // Reset timer counter
	// Using fast PWM mode for timer 0, setting up timer registers
	// Set OCR0A to percentage of the timer range (0 to 255)
	OCR0A = percentage*255;
	// Setting TCCR0A using bitwise OR to combine required bits
	TCCR0A = TCCR0A_CLEAR_A | TCCR0A_FAST_PWM_FULL_RANGE;
	// Use 1x clock divider for highest PWM frequency
	TCCR0B = TCCR0B_CLK_1x;
	// Timer has been started by setting clock divider
}

void init_ADC()
{
	// First two bits select Vref to Vcc (01)
	// Third bit left-justifies result (1)
	// Fourth bit not used
	// Last 4 bits selects ADC pin for mux to connect (A0, 0000)
	ADMUX = 0b01100000;
	// First bit enables ADC (1)
	// Second bit starts conversion (1), aiming for continuous mode, so not requiring re-setting.
	// Third it enables auto-triggering (1)
	// Bit 4 is set on interrupts
	// Bit 5 enables ADC interrupt
	// Last 3 bits sets clock prescaling for ADC conversion rate. Using slowest rate, /128 (111)
	ADCSRA = 0b11101111;
}

// Set up pin states and values, and initialize any internal hardware which
// has a consistent use in the program
void init()
{
	// Initialize button press flag to false
	button_pressed = 0;
	// Initialize button 2 pressed flag to false
	direction_change = 0;
	// Initialize potentiometer change flag to false
	dutycycle_change_adc = 0;
	// Initialize motor direction as forwards (NOT false)
	direction = !0;
	
	// Port D's only outputs are motor PWM pins, so set port D to output these pins
	DDRD = DDRD_PIN_6_OUT;
	// Initialize port D output to all zeros (output pins off)
	PORTD = 0x00;
	// Set port B output pins as required for direction selectors
	DDRB = PORTB_OUTPUTS;
	// Set port B output values by running the direction setting once, using the initial value
	update_motor_direction();
	// Start with outputting the first PWM duty cycle (0)
	next_duty_cycle_idx = 0;
	
	// Set up interrupts for button presses (falling-edge)
	EIMSK = (1 << INT0) | (1 << INT1); // Enable INT0 and INT1 interrupts
	EICRA = EICRA_FALLING_INT0 | EICRA_FALLING_INT1; // Set interrupt trigger to falling edge on INT0
	sei(); // Enable interrupts
}

int main(void)
{
	// Run initialization function
	init();
	#ifdef USE_ADC
	init_ADC();
	#endif
	// Set initial PWM output (which starts at 0%) and prepare next cycle index
	set_pwm_dutycycle(DUTY_CYCLE_VALUES[next_duty_cycle_idx++]);
	// Main Program Loop
    while (1) 
    {
		#ifdef USE_ADC
		if (dutycycle_change_adc)
		{
			// Update previously-set ADC sample value
			prev_adc_val = new_adc_val;
			uint8_t dutycycle = prev_adc_val;
			set_pwm_dutycycle(dutycycle);
			dutycycle_change_adc = 0;
		}
		#endif
		if (direction_change)
		{
			// Invert the direction flag
			direction = !direction;
			update_motor_direction();
			direction_change = 0;
		}
		#ifndef USE_ADC
		if (button_pressed)
		{
			// Update the last duty cycle index, before next index is updated.
			set_pwm_dutycycle(DUTY_CYCLE_VALUES[next_duty_cycle_idx++]);
			if (next_duty_cycle_idx >= N_DUTY_CYCLE_VALUES)
			{
				next_duty_cycle_idx = 0;
			}
			button_pressed = 0;
		}
		#endif
    }
}

