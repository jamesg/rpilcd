/*
 * RPILCD - A Raspberry Pi Audio Player.
 * Copyright (C) 2014 James Goode.
 */
#include <stdio.h>
#include "rpilcd.h"

int main(int argc, char* argv[])
{
    if (lcd_init(LCD_2X16) != 0)
    {
        fprintf(stderr, "Error initialising LCD\n");
        return 1;
    }
    lcd_2line("0123456789ABCDEF", "HD44780 LCD     ");
    lcd_close();
}

