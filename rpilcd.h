#ifndef RPILCD_H
#define RPILCD_H
/*
 * RPILCD - A Raspberry Pi Audio Player.
 * Copyright (C) 2014 James Goode.
 */

#define LCD_BUTTON_NONE 0

#define LCD_BUTTON_VOLUP 9
#define LCD_BUTTON_FILE 11

#define LCD_BUTTON_NOW 8
#define LCD_BUTTON_VOL 7

#define LCD_BUTTON_VOLDOWN 14
#define LCD_BUTTON_RW 15

#define LCD_BUTTON_PLAY 18
#define LCD_BUTTON_FF 17

/*
 * Button Layout
 *   9  11   8   7
 *  14  15  18  17
 */

/*!
 * Dimensions of the LCD screen, used to allocate buffers and translate
 * coordinates to addresses.
 */
enum lcd_screen_type_t { LCD_4X20, LCD_2X16 };
/*!
 * Initialise the LCD pins on the Raspberry Pi and clear the screen.
 */
int lcd_init(enum lcd_screen_type_t);
/*!
 * For use with 2x16 displays: write the two strings to the two lines of the
 * display.
 * \note Calls to this function are ignored if the LCD is not a 2 line type.
 */
void lcd_2line(const char*, const char*);
/*!
 * For use with 4x20 displays: write the four strings to the four lines of the
 * display.
 * \note Calls to this function are ignored if the LCD is not a 4 line type.
 */
void lcd_4line(const char*, const char*, const char*, const char*);
/*!
 * Clear the LCD.
 */
void lcd_clear();
/*!
 * Stop the LCD output, leaving the display contents as is.
 */
void lcd_close();
/*!
 * Poll the state of the GPIO pins to discover if a single button is pressed.
 */
int poll_button_press();
/*!
 * Update the content of the LCD to match the given string.  Only updates
 * characters that have changed.
 */
void lcd_update(const char*);
/*!
 * \return The number of characters the LCD can display.
 */
int lcd_size();
/*!
 * \return The number of characters on one row of the LCD.
 */
int lcd_width();
/*!
 * \return The number of character rows on the LCD.
 */
int lcd_height();

#endif

