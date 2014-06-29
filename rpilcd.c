/*
 * RPILCD - A Raspberry Pi Audio Player.
 * Copyright (C) 2014 James Goode.
 */
/*

Raspberry Pi HD44780 LCD Contoller
==================================

Pin Definitions
---------------

LCD Pin             Raspberry Pi Pin        BCM2835 Pin
D4 (11)             P1 16                   GPIO 22
D5 (12)             P1 18                   GPIO 23
D6 (13)             P1 19                   GPIO 24
D7 (14)             P1 21                   GPIO 25
RS (4)              P1 14                   GPIO 10
E (6)               P1 15                   GPIO  4

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SIMULATE_LCD
#include <bcm2835.h>
#endif
#include "rpilcd.h"

// These are GPIO pin numbers, not Raspberry Pi pin numbers.
#define LCDPIN_D4   22
#define LCDPIN_D5   23
#define LCDPIN_D6   24
#define LCDPIN_D7   25
#define LCDPIN_RS   10
#define LCDPIN_E    4

// LCD data pins (four pins, transfers one nibble at a time).
const int lcdpin[] = { LCDPIN_D4, LCDPIN_D5, LCDPIN_D6, LCDPIN_D7 };

// Currently displayed on screen; to save time and stop flicker when updating.
char *screen_buffer;

// Screen type - determines buffer size and memory address offsets.
enum lcd_screen_type_t lcd_screen_type;

#define LCD_DELAY   1

#ifdef SIMULATE_LCD
void lcd_cmd(unsigned char c, int char_mode)
{
}
#else // SIMULATE_LCD
void lcd_cmd(unsigned char c, int char_mode)
{
    int i = 0;
    bcm2835_delay(LCD_DELAY);
    bcm2835_gpio_write(LCDPIN_RS, char_mode);

    for (i = 0; i < 4; i++) bcm2835_gpio_write(lcdpin[i], 0);
    for (i = 0; i < 4; i++) bcm2835_gpio_write(lcdpin[i], ((c >> (i + 4)) & 1));

    bcm2835_gpio_write(LCDPIN_E, HIGH);
    bcm2835_delay(LCD_DELAY);
    bcm2835_gpio_write(LCDPIN_E, LOW);

    for (i = 0; i < 4; i++) bcm2835_gpio_write(lcdpin[i], 0);
    for (i = 0; i < 4; i++) bcm2835_gpio_write(lcdpin[i], ((c >> i) & 1));

    bcm2835_gpio_write(LCDPIN_E, HIGH);
    bcm2835_delay(LCD_DELAY);
    bcm2835_gpio_write(LCDPIN_E, LOW);

    bcm2835_delay(LCD_DELAY);
}
#endif // SIMULATE_LCD

int lcd_pos_to_addr(int pos)
{
    switch (lcd_screen_type)
    {
        case LCD_4X20:
        switch (pos/20)
        {
            case 0:
            return 128 + (pos%20);
            case 1:
            return 192 + (pos%20);
            case 2:
            return 148 + (pos%20);
            case 3:
            return 212 + (pos%20);
        }
        case LCD_2X16:
        switch (pos/16)
        {
            case 0:
            return 128 + (pos%16);
            case 1:
            return 192 + (pos%16);
        }
    }
}

void lcd_line(const char* line, int n)
{
	int i = 0;
    // If line is null, print a line of n spaces.
    if (line == 0)
    {
        fprintf(stderr, "|%-*s|\n", n, "");
        for (i = 0; i < n; i++) lcd_cmd(' ', 1);
        return;
    }

    int len = strlen(line);
    len = (len > n)?n:len;

    // %-*s pads spaces but does not limit the length of a line.
    char *line_dup = strdup(line);
    if (strlen(line) > n) line_dup[n] = 0;
	fprintf(stderr, "|%-*s|\n", n, line_dup);
    free(line_dup);

    for (i = 0; i < n; i++) lcd_cmd((i < len)?line[i]:' ', 1);
}

void lcd_2line(const char* s1, const char* s2)
{
    if (lcd_height() != 2) return;
#ifdef SIMULATE_LCD
	fprintf(stderr, "+----------------+\n");
    fprintf(stderr, "|%-*s|\n", lcd_width(), s1);
    fprintf(stderr, "|%-*s|\n", lcd_width(), s2);
	fprintf(stderr, "+----------------+\n");
#endif
    char s[81];
    snprintf((char*)&s[0], 17, "%-*s", 17, s1);
    snprintf((char*)&s[16], 17, "%-*s", 17, s2);
    lcd_update((char*)&s);
}

void lcd_4line(const char* s1, const char* s2, const char* s3, const char* s4)
{
    if (lcd_height() != 4) return;
#ifdef SIMULATE_LCD
	fprintf(stderr, "+--------------------+\n");
    fprintf(stderr, "|%-*s|\n", lcd_width(), s1);
    fprintf(stderr, "|%-*s|\n", lcd_width(), s2);
    fprintf(stderr, "|%-*s|\n", lcd_width(), s3);
    fprintf(stderr, "|%-*s|\n", lcd_width(), s4);
	fprintf(stderr, "+--------------------+\n");
#endif
    char s[81];
    snprintf((char*)&s[0], 21, "%-*s", 21, s1);
    snprintf((char*)&s[20], 21, "%-*s", 21, s2);
    snprintf((char*)&s[40], 21, "%-*s", 21, s3);
    snprintf((char*)&s[60], 21, "%-*s", 21, s4);
    lcd_update((char*)&s);
#ifdef SIMULATE_LCD
    fprintf(stderr, "-- %s --\n", (char*)&s);
#endif
}

int lcd_size()
{
    switch (lcd_screen_type)
    {
        case LCD_4X20: return 80;
        case LCD_2X16: return 32;
    }
}

int lcd_width()
{
    switch (lcd_screen_type)
    {
        case LCD_4X20: return 20;
        case LCD_2X16: return 16;
    }
}

int lcd_height()
{
    switch (lcd_screen_type)
    {
        case LCD_4X20: return 4;
        case LCD_2X16: return 2;
    }
}

void lcd_update(const char* s)
{
    int i = 0;
    while(i < lcd_size())
    {
        if(s[i] != screen_buffer[i])
        {
            // Move to i on the screen.
            lcd_cmd(lcd_pos_to_addr(i), 0);
            lcd_cmd(s[i], 1);
            screen_buffer[i] = s[i];
            i++;
            while(
                    s[i] != screen_buffer[i] &&
                    i < lcd_size() && i%lcd_width() != 0
                    )
            {
                lcd_cmd(s[i], 1);
                screen_buffer[i] = s[i];
                i++;
            }
        } else i++;
    }
}

int lcd_init(enum lcd_screen_type_t t)
{
    lcd_screen_type = t;
    switch (lcd_screen_type)
    {
    case LCD_4X20:
        screen_buffer = malloc(80);
        memset(screen_buffer, ' ', 80);
        break;
    case LCD_2X16:
        screen_buffer = malloc(32);
        memset(screen_buffer, ' ', 32);
        break;
    }

#ifndef SIMULATE_LCD
    if (!bcm2835_init())
    {
        fprintf(stderr, "Error initialising BCM2835\n");
        return 1;
    }

    int i = 0;
    for (i = 0; i < 4; i++)
        bcm2835_gpio_fsel(lcdpin[i], BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(LCDPIN_RS, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(LCDPIN_E, BCM2835_GPIO_FSEL_OUTP);

    bcm2835_gpio_fsel(LCD_BUTTON_VOLUP, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(LCD_BUTTON_FILE, BCM2835_GPIO_FSEL_INPT);

    bcm2835_gpio_fsel(LCD_BUTTON_NOW, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(LCD_BUTTON_VOL, BCM2835_GPIO_FSEL_INPT);

    bcm2835_gpio_fsel(LCD_BUTTON_VOLDOWN, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(LCD_BUTTON_RW, BCM2835_GPIO_FSEL_INPT);

    bcm2835_gpio_fsel(LCD_BUTTON_PLAY, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(LCD_BUTTON_FF, BCM2835_GPIO_FSEL_INPT);
#endif // SIMULATE_LCD

    // Initialise HD44780.

    // Set 8 bit mode, set 4 bit mode (must use an even number of commands
    // because lcd_cmd sends two commands).
    lcd_cmd(0x33, 0); // 0011 0011
    lcd_cmd(0x32, 0); // 0011 0010
    // Set all function settings - 4 bit, 1/16 duty, 5x10 dot font.
    lcd_cmd(0x28, 0); // 0010 1000
    // Set display on, cursor off, not blinking.
    lcd_cmd(0x0c, 0); // 0000 1100
    // Set entry mode: cursor direction left to right, no display shift.
    lcd_cmd(0x06, 0); // 0000 0110
    // Clear display.
    lcd_cmd(0x01, 0); // 0000 0001
    return 0;
}

void lcd_clear()
{
    // 0x01 clears the screen.
    lcd_cmd(0x01, 0);
}

void lcd_close()
{
#ifndef SIMULATE_LCD
    bcm2835_close();
#endif
}

int poll_button_press()
{
#ifndef SIMULATE_LCD
    if (bcm2835_gpio_lev(LCD_BUTTON_VOLUP) == LOW) return LCD_BUTTON_VOLUP;
    if (bcm2835_gpio_lev(LCD_BUTTON_FILE) == LOW) return LCD_BUTTON_FILE;

    if (bcm2835_gpio_lev(LCD_BUTTON_NOW) == LOW) return LCD_BUTTON_NOW;
    if (bcm2835_gpio_lev(LCD_BUTTON_VOL) == LOW) return LCD_BUTTON_VOL;

    if (bcm2835_gpio_lev(LCD_BUTTON_VOLDOWN) == LOW) return LCD_BUTTON_VOLDOWN;
    if (bcm2835_gpio_lev(LCD_BUTTON_RW) == LOW) return LCD_BUTTON_RW;

    if (bcm2835_gpio_lev(LCD_BUTTON_PLAY) == LOW) return LCD_BUTTON_PLAY;
    if (bcm2835_gpio_lev(LCD_BUTTON_FF) == LOW) return LCD_BUTTON_FF;
#endif
    return LCD_BUTTON_NONE;
}

