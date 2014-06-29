#ifndef PLAY_H
#define PLAY_H
/*
 * RPILCD - A Raspberry Pi Audio Player.
 * Copyright (C) 2014 James Goode.
 */

#include <dirent.h>

#define PLAY_SAMPLERATE 22050

/*!
 * A single button press (or simulated button press).
 */
enum button_press_t
{
    UP,
    DOWN,
    PLAY,
    MODE,
    QUIT
};

/*!
 * The current mode of the player (file browser, now playing).
 */
enum mode_t
{
    FILES,
    NOW,
    VOL
};

/*!
 * The current state of the player (stopped, paused, playing).
 */
enum player_state_t
{
    STOPPED,
    PAUSED,
    PLAYING
};

/*!
 * A linked list representing the playlist.
 */
struct playlist_t
{
    char *path, *title;
    struct playlist_t *next;
};

/*!
 * Send a track to SDL_mixer to be played immediately (in SDL's own thread).
 */
void play_music(const char* path);

/*!
 * Start playing the next track in the playlist, unless there is a track
 * currently playing.
 */
void queue_next();

/*!
 * Wait for tracks to be added to the global playlist and start playing them.
 * To be called as a thread.
 */
void *next_track_thread(void *v);

/*!
 * Append a track to the global playlist.  This function is not thread safe,
 * so playlist_mutex must be locked when this function is called.  This is to
 * allow callers to append multiple items at once.
 */
void append_to_playlist(const char* path, const char *title);

/*!
 * Perform the stat system call for a file in the current working directory.
 * This function is not thread safe, it requires that the directory list mutex
 * is locked.
 */
int wdstat(const char*, struct stat*);

/*!
 * Clear and free all space used by the global list of files in the current
 * working directory.
 */
void free_directory_list();

/*!
 * Check if the given dirent represents an MP3 file.
 */
int is_mp3(const struct dirent*);

/*!
 * Check if the given dirent represents either an MP3 or directory.
 */
int is_mp3_or_dir(const struct dirent*);

/*!
 * Comparision for ordering files in directory lists (alphabetical,
 * directories first).
 */
int dirname_cmp(const struct dirent**, const struct dirent**);

/*!
 * Change the current working directory and store a list of new directory
 * contents in the global directory list.
 */
void change_directory(const char*);

/*!
 * Change the current working directory relative to the current working
 * directory and store a list of new directory contents in the global directory
 * list.
 */
void wd_change_directory(const char*);

/*!
 * Add all MP3s in the current working directory to the global queue, starting
 * at start.  If start is NULL, all MP3s in the current directory are added.
 */
void wd_queue_directory(const char*);

// Draw the directory list on the LCD.
void draw_directory_list();

/*!
 * Draw the directory list on the LCD.
 */
void draw_directory_list();

/*!
 * Move the cursor position in the directory list (positive numbers move down
 * the list, negative numbers move up.
 */
void move_list(int);
/*!
 * Move the directory cursor position one space up.
 */
void move_list_up();
/*!
 * Move the directory cursor position one space down.
 */
void move_list_down();

/*!
 * Generate a string representing the current position (in minutes and
 * seconds).  The returned pointer must be freed with free().
 */
char *position_string();

/*!
 * Draw the 'now playing' screen.
 */
void draw_now_playing();

/*!
 * Generate a step in the scrolling text animation.
 *
 * \param length Length of the displayed string.
 * \param scroll_pos Stage in the scroll animation to show.
 * \note The returned string must be freed by the caller.
 * \note The returned string may be longer than the length parameter.
 */
char *scroll_text(const char *text, int length, int scroll_pos);

/*!
 * Draw the volume change screen.
 */
void draw_vol();

/*!
 * Initialise the music player, including SDL functions and the LCD screen.
 */
void play_init();

/*!
 * Process a button press event.
 * \pre The files mode is currently selected.
 */
void button_press_files(enum button_press_t button);
/*!
 * Process a button press event.
 * \pre The now playing mode is currently selected.
 */
void button_press_now(enum button_press_t button);
/*!
 * Process a button press event.
 * \pre The volume control mode is currently selected.
 */
void button_press_vol(enum button_press_t button);

/*!
 * Thread that listens for button presses (or simulated button presses) and
 * signals button_press_mutex when one occurs.  The button press is stored in
 * the global button_press variable.
 */
void *button_press_thread(void*);

#endif

