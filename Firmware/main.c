// ================================================================
// Author of Code: MOHD SHAFI RAZA ANSARI
// Date: 29 June 2026
// Comfort ECU - PIC18F45K20   FINAL VERSION v3
//  Compiler : MPLAB XC8 v2.x  |  IDE: MPLAB X
//
//  PIN MAP (verified manually):
//  -----------------------------------------------
//  INPUTS:
//    RA0/AN0  -> LDR sensor (ADC, ambient light + auto headlight)
//               Voltage divider: +5V-R6-junction(RA0)-LDR-GND
//               DARK  = HIGH ADC value (LDR high resistance)
//               BRIGHT = LOW ADC value (LDR low resistance)
//    RB0      -> SW_UP    (window up,   pull-down R1, HIGH when pressed)
//    RB1      -> SW_DOWN  (window down, pull-down R2, HIGH when pressed)
//    RB2      -> DOOR_SW  (pull-down R3, LOW=door open, HIGH=door closed)
//    RB3      -> SEATBELT_SW (pull-down R4, LOW=unfastened, HIGH=fastened)
//    RC2      -> DHT11 DATA (bi-directional, R14 4.7k pull-up)
//
//  OUTPUTS:
//    RB4      -> Buzzer (Q1/BC547 via R12)
//    RB5      -> Door Lock Indicator D8 LED-WHITE (via R17)
//    RC0      -> L293D IN1 (window motor direction 1)
//    RC1      -> L293D IN2 (window motor direction 2)
//    RC3      -> HVAC Blower Motor (Q3/PN2222 via R5)
//    RC4      -> Seat Cooling Fan  (Q2/PN2222 via R7)
//    RC5      -> Heater Valve    D2 LED-YELLOW (via R8)
//    RC6      -> AC Compressor   D3 LED-BLUE   (via R9)
//    RC7      -> Seat Heater     D4 LED-ORANGE (via R10)
//    RD0      -> Ambient Light   D5 LED-PURPLE (via R11)
//    RD1      -> Auto Headlight  D6 LED-WHITE  (via R13)
//    RD2      -> LCD RS  (pin 4)
//    RD3      -> LCD E   (pin 6)   [LCD RW pin 5 tied to GND]
//    RD4-RD7  -> LCD D4-D7 (pins 11-14, 4-bit mode)
// ================================================================

#include <xc.h>
#include <stdio.h>
#include <string.h>

// ----------------------------------------------------------------
//  CONFIG BITS
// ----------------------------------------------------------------
#pragma config FOSC   = HS        // Changed to ext oscillator (HS)Internal oscillator, RA6/RA7 as GPIO
#pragma config WDTEN  = OFF       // Watchdog timer OFF
#pragma config LVP    = OFF       // Low voltage programming OFF
#pragma config MCLRE  = ON        // MCLR pin enabled
#pragma config PBADEN = OFF       // PORTB pins digital on reset

#define _XTAL_FREQ 20000000UL      // 20 MHz

// ================================================================
//  PIN DEFINITIONS
// ================================================================

// --- Inputs (PORTB) ---
#define SW_UP           PORTBbits.RB0   // HIGH when pressed
#define SW_DOWN         PORTBbits.RB1   // HIGH when pressed
#define DOOR_SW         PORTBbits.RB2   // LOW=open, HIGH=closed
#define SEATBELT_SW     PORTBbits.RB3   // LOW=unfastened, HIGH=fastened

// --- Outputs (PORTB) ---
#define BUZZER          LATBbits.LATB4
#define DOOR_LOCK_LED   LATBbits.LATB5

// --- Window Motor (PORTC) ---
#define MOTOR_IN1       LATCbits.LATC0
#define MOTOR_IN2       LATCbits.LATC1

// --- DHT11 (RC2, bi-directional) ---
#define DHT11_TRIS      TRISCbits.TRISC2
#define DHT11_LAT       LATCbits.LATC2
#define DHT11_PORT      PORTCbits.RC2

// --- HVAC + Seat (PORTC) ---
#define BLOWER_MOTOR    LATCbits.LATC3
#define SEAT_COOL_FAN   LATCbits.LATC4
#define HEATER_VALVE    LATCbits.LATC5
#define AC_COMPRESSOR   LATCbits.LATC6
#define SEAT_HEATER     LATCbits.LATC7

// --- Lighting (PORTD) ---
#define AMBIENT_LED     LATDbits.LATD0
#define HEADLIGHT_LED   LATDbits.LATD1

// --- LCD (PORTD) ---
// RD2=RS, RD3=E, RD4-RD7=D4-D7, RW tied to GND
#define LCD_RS          LATDbits.LATD2
#define LCD_E           LATDbits.LATD3

// ================================================================
//  THRESHOLDS
// ================================================================
// Temperature (degrees C)
#define TEMP_HEAT_ON    10    // Start heating below 10C
#define TEMP_HEAT_OFF   20    // Stop heating at 20C
#define TEMP_COOL_ON    25    // Start cooling above 25C
#define TEMP_COOL_OFF   18    // Stop cooling at 18C

// LDR (10-bit ADC 0-1023)
// Wiring: +5V -> R6 -> RA0 -> LDR -> GND
// DARK  = LDR high resistance = HIGH voltage at RA0 = HIGH ADC
// BRIGHT = LDR low resistance = LOW voltage at RA0 = LOW ADC
#define LDR_AMBIENT_TH   800  // ADC ABOVE this = dark -> Ambient LED ON
#define LDR_HEADLIGHT_TH 600  // ADC ABOVE this = very dark -> Headlight ON

// LCD rotation interval (number of 50ms ticks)
#define LCD_ROTATE_TICKS 40   // 40 x 50ms = 2 seconds per message

// ================================================================
//  GLOBALS
// ================================================================
unsigned char g_temp       = 15;   // Default 15C (triggers heating on start)
unsigned char g_hum        = 50;
unsigned char g_hvac_heat  = 0;
unsigned char g_hvac_cool  = 0;

// ================================================================
//  LCD DRIVER
//  4-bit mode: RD4-RD7=data, RD2=RS, RD3=E, RW tied to GND
//
//  KEY FIX: RS(RD2) and E(RD3) share LATD with LED outputs
//  RD0=AMBIENT_LED, RD1=HEADLIGHT_LED must be preserved.
//  We build the full LATD byte explicitly each write.
// ================================================================

// Write one nibble to LCD, preserving RD0 and RD1
void LCD_WriteNibble(unsigned char nibble, unsigned char rs) {
    unsigned char led_bits = LATD & 0x03;          // Save RD0, RD1
    unsigned char rs_bit   = rs ? 0x04 : 0x00;     // RS on RD2
    unsigned char data     = nibble & 0xF0;         // Data on RD4-RD7

    // Write data with E=0
    LATD = led_bits | rs_bit | data;
    __delay_us(1);

    // Pulse E high then low
    LATD |= 0x08;                                   // E=1 (RD3)
    __delay_us(2);
    LATD &= ~0x08;                                  // E=0 (RD3)
    __delay_us(100);
}

// Send full byte as two nibbles (high nibble first)
void LCD_Send(unsigned char data, unsigned char rs) {
    LCD_WriteNibble(data & 0xF0, rs);
    LCD_WriteNibble((unsigned char)(data << 4), rs);
    // Clear and home commands need extra time
    if (data == 0x01 || data == 0x02)
        __delay_ms(2);
    else
        __delay_us(50);
}

#define LCD_Cmd(c)  LCD_Send((c), 0)
#define LCD_Chr(c)  LCD_Send((c), 1)

void LCD_Init(void) {
    __delay_ms(50);    // Power-on delay >40ms

    unsigned char leds = LATD & 0x03;

    // HD44780 4-bit initialisation sequence (datasheet p.46)
    LATD = leds | 0x30; __delay_us(1);
    LATD |= 0x08; __delay_us(2); LATD &= ~0x08; __delay_ms(5);

    LATD = leds | 0x30; __delay_us(1);
    LATD |= 0x08; __delay_us(2); LATD &= ~0x08; __delay_us(200);

    LATD = leds | 0x30; __delay_us(1);
    LATD |= 0x08; __delay_us(2); LATD &= ~0x08; __delay_us(100);

    // Switch to 4-bit
    LATD = leds | 0x20; __delay_us(1);
    LATD |= 0x08; __delay_us(2); LATD &= ~0x08; __delay_us(100);

    LCD_Cmd(0x28);   // 4-bit, 2 lines, 5x8 font
    LCD_Cmd(0x08);   // Display OFF
    LCD_Cmd(0x01);   // Clear display
    LCD_Cmd(0x06);   // Entry mode: increment, no shift
    LCD_Cmd(0x0C);   // Display ON, cursor OFF, blink OFF
}

void LCD_SetCursor(unsigned char row, unsigned char col) {
    LCD_Cmd(row == 0 ? (unsigned char)(0x80 + col)
                     : (unsigned char)(0xC0 + col));
}

void LCD_Print(const char *s) {
    while (*s) LCD_Chr((unsigned char)(*s++));
}

// Print a string centred on a row (pads with spaces automatically)
void LCD_PrintCentred(unsigned char row, const char *s) {
    unsigned char len = (unsigned char)strlen(s);
    unsigned char col = len < 16 ? (unsigned char)((16 - len) / 2) : 0;
    LCD_SetCursor(row, col);
    LCD_Print(s);
}

// Print exactly 16 chars (pads with spaces if shorter)
void LCD_PrintLine(unsigned char row, const char *s) {
    LCD_SetCursor(row, 0);
    unsigned char i = 0;
    while (s[i] && i < 16) { LCD_Chr((unsigned char)s[i]); i++; }
    while (i < 16)          { LCD_Chr(' '); i++; }
}

// ================================================================
//  ADC DRIVER
// ================================================================
void ADC_Init(void) {
    ANSEL  = 0x01;           // AN0 (RA0) analogue only
    ANSELH = 0x00;           // All other analogue pins -> digital
    ADCON0 = 0x01;           // Selecting AN0, ADC ON
    ADCON1 = 0x0F;           // Vref=VDD/VSS, use ANSEL for channel select
    ADCON2 = 0b10101010;     // Right-justified, 12 TAD, Fosc/32
}

unsigned int ADC_Read(unsigned char ch) {
    ADCON0 = (unsigned char)(((unsigned char)(ch << 2)) | 0x01);
    __delay_us(30);
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO_NOT_DONE);
    return (unsigned int)((ADRESH << 8) | ADRESL);
}

// ================================================================
//  DHT11 DRIVER (RC2)
// ================================================================
void DHT11_Idle(void) {
    DHT11_TRIS = 0;
    DHT11_LAT  = 1;
}

static unsigned char DHT11_ReadByte(void) {
    unsigned char i, byte = 0;
    unsigned int  t;
    for (i = 0; i < 8; i++) {
        t = 0; while(!DHT11_PORT)
{
    if(++t > 30000)
    {
        DHT11_Idle();
        return 0;
    }
}       //while (!DHT11_PORT && ++t < 10000);  // Wait for HIGH
        __delay_us(45);
        if (DHT11_PORT) byte |= (unsigned char)(1 << (7 - i));
        t = 0; while (DHT11_PORT && ++t < 10000);   // Wait for LOW
    }
    return byte;
}

unsigned char DHT11_Read(void) {
    unsigned char h_int, h_dec, t_int, t_dec, chk;
    unsigned int  t = 0;

    // Send start pulse
    DHT11_TRIS = 0;
    DHT11_LAT  = 0;
    __delay_ms(18);
    DHT11_LAT  = 1;
    __delay_us(40);
    DHT11_TRIS = 1;            // Switch to input

    // Wait for response LOW
    __delay_us(10);
    t = 0; while (DHT11_PORT  && ++t < 10000);
    if (t >= 10000) { DHT11_Idle(); return 0; }

    // Wait for response HIGH
    t = 0; while (!DHT11_PORT && ++t < 10000);
    if (t >= 10000) { DHT11_Idle(); return 0; }

    // Wait for data start (response HIGH -> LOW)
    t = 0; while (DHT11_PORT  && ++t < 10000);
    if (t >= 10000) { DHT11_Idle(); return 0; }

    // Read 5 bytes
    h_int = DHT11_ReadByte();
    h_dec = DHT11_ReadByte();
    t_int = DHT11_ReadByte();
    t_dec = DHT11_ReadByte();
    chk   = DHT11_ReadByte();

    DHT11_Idle();

    if (chk == (unsigned char)(h_int + h_dec + t_int + t_dec)) {
        g_temp = t_int;
        g_hum  = h_int;
        return 1;
    }
    return 0;   // Checksum fail: keep previous values
}

// ================================================================
//  FEATURE 1: POWER WINDOWS
//  SW_UP  (RB0) pressed -> motor UP   (IN1=1, IN2=0)
//  SW_DOWN(RB1) pressed -> motor DOWN (IN1=0, IN2=1)
//  Neither pressed       -> motor STOP
// ================================================================
void Control_Windows(void) {
    if (SW_UP && !SW_DOWN) {
        MOTOR_IN1 = 1;
        MOTOR_IN2 = 0;
    } else if (SW_DOWN && !SW_UP) {
        MOTOR_IN1 = 0;
        MOTOR_IN2 = 1;
    } else {
        MOTOR_IN1 = 0;
        MOTOR_IN2 = 0;
    }
}

// ================================================================
//  FEATURE 2 + 3: CLIMATE CONTROL + SEAT TEMPERATURE
//
//  HEATING (temp < 10C):
//    -> Heater Valve ON, HVAC Blower ON, Seat Heater ON
//    -> Stays ON until temp reaches 20C
//
//  COOLING (temp > 25C):
//    -> AC Compressor ON, HVAC Blower ON, Seat Cooling Fan ON
//    -> Stays ON until temp drops to 18C
//
//  NORMAL (10-25C range): all OFF
// ================================================================
void Control_Climate(void) {
    // Heating hysteresis
    if (!g_hvac_heat && (g_temp < TEMP_HEAT_ON))
        g_hvac_heat = 1;
    else if (g_hvac_heat && (g_temp >= TEMP_HEAT_OFF))
        g_hvac_heat = 0;

    // Cooling hysteresis
    if (!g_hvac_cool && (g_temp > TEMP_COOL_ON))
        g_hvac_cool = 1;
    else if (g_hvac_cool && (g_temp <= TEMP_COOL_OFF))
        g_hvac_cool = 0;

    if (g_hvac_heat) {
        // Heating mode
        HEATER_VALVE  = 1;
        BLOWER_MOTOR  = 1;
        SEAT_HEATER   = 1;
        AC_COMPRESSOR = 0;
        SEAT_COOL_FAN = 0;
    } else if (g_hvac_cool) {
        // Cooling mode
        HEATER_VALVE  = 0;
        BLOWER_MOTOR  = 1;
        SEAT_HEATER   = 0;
        AC_COMPRESSOR = 1;
        SEAT_COOL_FAN = 1;
    } else {
        // Normal range - all off
        HEATER_VALVE  = 0;
        BLOWER_MOTOR  = 0;
        SEAT_HEATER   = 0;
        AC_COMPRESSOR = 0;
        SEAT_COOL_FAN = 0;
    }
}

// ================================================================
//  FEATURE 4: LIGHTING (LDR on RA0/AN0)
//
//  LDR wiring: +5V -> R6(10k) -> RA0 -> LDR -> GND
//  DARK  = LDR high resistance = HIGH voltage at RA0 = HIGH ADC
//  BRIGHT = LDR low resistance = LOW voltage at RA0 = LOW ADC
// ================================================================
void Control_Lighting(unsigned int ldr) {
    // Ambient LED: ON when dark (high ADC)
    AMBIENT_LED   = (ldr > LDR_AMBIENT_TH)   ? 1 : 0;

    // Headlight: ON when very dark (very high ADC)
    HEADLIGHT_LED = (ldr > LDR_HEADLIGHT_TH) ? 1 : 0;
}

// ================================================================
//  DOOR LOCK INDICATOR
//  RB2 uses pull-down resistor R3:
//    Switch NOT pressed (door open)   = RB2 LOW  -> LED ON
//    Switch PRESSED     (door closed) = RB2 HIGH -> LED OFF
// ================================================================
void Control_DoorLock(void) {
    DOOR_LOCK_LED = DOOR_SW ? 0 : 1;
    //  DOOR_SW=0 (open,  not pressed) -> LED = 1 (ON)
    //  DOOR_SW=1 (closed, pressed)    -> LED = 0 (OFF)
}

// ================================================================
//  FEATURE 5: SEATBELT CHIME (NON-BLOCKING)
//
//  SEATBELT_SW LOW  (not fastened) -> Buzzer alternates high/low tone
//  SEATBELT_SW HIGH (fastened)     -> Buzzer OFF
//
//  High tone: toggle every tick (~50ms) = ~10Hz (audible in Proteus)
//  Low tone:  toggle every 2 ticks (~100ms) = ~5Hz
// ================================================================
void Control_Seatbelt(unsigned char tick) {
    if (SEATBELT_SW) {
        BUZZER = 0;
        return;
    }

    // Divide ticks into 80-tick cycle (4 seconds total)
    unsigned char phase = tick % 80;

    if (phase < 40) {
        // High tone: fast toggle (every tick)
        BUZZER = (unsigned char)(phase % 2);
    } else {
        // Low tone: slow toggle (every 2 ticks)
        BUZZER = (unsigned char)((phase % 4) < 2 ? 1 : 0);
    }
}

// ================================================================
//  LCD DISPLAY - ROTATING MESSAGES
//
//  Line 1: ALWAYS shows Temperature and Humidity from DHT11
//  Line 2: Rotates through all system statuses every 2 seconds
//          so ALL information is visible regardless of state.
//
//  Rotation order:
//    Slot 0: Climate status (heating/cooling/normal)
//    Slot 1: Door status
//    Slot 2: Lighting status
//    Slot 3: Seatbelt status
//
//  Seatbelt alert ALSO shown on line 1 T/H row when unfastened
//  so it is never missed even during rotation.
// ================================================================
char lcd_buf[17];

void LCD_UpdateDisplay(unsigned char tick) {
    // --- Line 1: Temperature + Humidity (always) ---
    LCD_SetCursor(0, 0);
    if (!SEATBELT_SW) {
        // Seatbelt warning blinks on line 1 alongside temp
        sprintf(lcd_buf, "T:%2uC ** BELT! ", (unsigned int)g_temp);
    } else {
        sprintf(lcd_buf, "T:%2uC  H:%2u%%    ",
                (unsigned int)g_temp, (unsigned int)g_hum);
    }
    LCD_Print(lcd_buf);

    // --- Line 2: Rotating status (changes every 2 seconds) ---
    unsigned char slot = (unsigned char)((tick / LCD_ROTATE_TICKS) % 4);

    LCD_SetCursor(1, 0);
    switch (slot) {
        case 0:  // Climate status
            if (g_hvac_heat)
                LCD_Print("HEATING ON      ");
            else if (g_hvac_cool)
                LCD_Print("COOLING ON      ");
            else
                LCD_Print("Climate: Normal ");
            break;

        case 1:  // Door status
            if (!DOOR_SW)
                LCD_Print("DOOR OPEN!      ");
            else
                LCD_Print("Door: Closed    ");
            break;

        case 2:  // Lighting status
            if (HEADLIGHT_LED)
                LCD_Print("Headlights: ON  ");
            else if (AMBIENT_LED)
                LCD_Print("Ambient: ON     ");
            else
                LCD_Print("Lights: OFF     ");
            break;

        case 3:  // Seatbelt status
            if (!SEATBELT_SW)
                LCD_Print("SEATBELT ALERT! ");
            else
                LCD_Print("Seatbelt: OK    ");
            break;
    }
}

// ================================================================
//  MAIN
// ================================================================
void main(void) {

//    // 8 MHz internal oscillator
//    OSCCONbits.IRCF = 0b110;
//    OSCCONbits.SCS  = 0b00;

    // --- Port directions ---
    TRISA = 0xFF;    // RA0 = ADC input
    TRISB = 0x0F;    // RB0-RB3 = inputs, RB4-RB7 = outputs
    TRISC = 0x00;    // All PORTC outputs (RC2 toggled by DHT11)
    TRISD = 0x00;    // All PORTD outputs (LCD + LEDs)
    TRISE = 0x0F;    // RE0-RE2 inputs (unused), RE3=MCLR

    // --- Analogue/digital config ---
    ANSEL  = 0x01;   // Only AN0 (RA0) is analogue
    ANSELH = 0x00;   // All PORTB analogue channels -> digital

    // --- Clear all output latches ---
    LATB = 0x00;
    LATC = 0x00;
    LATD = 0x00;
    LATE = 0x00;

    // --- Disable CCP1 (shares RC2 with DHT11) ---
    CCP1CON = 0x00;

    // --- Initialise ADC ---
    ADC_Init();

    // --- DHT11 idle state ---
    DHT11_Idle();

    // --- Initialise LCD ---
    LCD_Init();

    // --- Startup splash screen ---
    LCD_PrintCentred(0, "Comfort ECU");
    LCD_PrintCentred(1, "Starting...");
    __delay_ms(2000);

    LCD_Cmd(0x01);   // Clear display
    __delay_ms(2);

    LCD_PrintCentred(0, "ECU Ready");
    LCD_PrintCentred(1, "Designd by Shafi");
    __delay_ms(1500);

    LCD_Cmd(0x01);
    __delay_ms(2);

    // --- First DHT11 read before entering main loop ---
    DHT11_Read();

    // --- Runtime variables ---
    unsigned int  ldr_val    = 0;
    unsigned char tick       = 0;    // 50ms tick counter (0-255, wraps)
    unsigned char dht_ticks  = 0;    // DHT11 read every 40 ticks = ~2s

    // ============================================================
    //  MAIN LOOP  (50ms per iteration - non-blocking)
    // ============================================================
    while (1) {

        // --- 1. Read LDR (every loop) ---
        ldr_val = ADC_Read(0);

        // --- 2. Read DHT11 every ~2 seconds ---
        dht_ticks++;
        if (dht_ticks >= 40) {
            DHT11_Read();
            dht_ticks = 0;
        }

        // --- 3. Run feature controllers ---
        Control_Windows();              // Power windows
        Control_Climate();              // HVAC + Seat temp
        Control_Lighting(ldr_val);      // Ambient + Headlight
        Control_DoorLock();             // Door lock indicator
        Control_Seatbelt(tick);         // Seatbelt chime (non-blocking)

        // --- 4. Update LCD ---
        LCD_UpdateDisplay(tick);

        // --- 5. Increment tick and wait ---
        tick++;
        __delay_ms(50);
    }
}