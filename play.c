/*
 * RPILCD - A Raspberry Pi Audio Player.
 * Copyright (C) 2014 James Goode.
 */
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "SDL/SDL.h"
#include "SDL/SDL_mixer.h"
#include "rpilcd.h"
#include "play.h"

#define LCD_BUTTON_PLAY_LCD_TYPE LCD_2X16

#define POSITION_STRING_LEN 5

#ifdef SIMULATE_LCD
#define DELAY_MILLIS(millis) SDL_Delay(millis)
#else
#define DELAY_MILLIS(millis) bcm2835_delay(millis)
#endif

int volume_level[] = {
    0, 5, 10, 17, 25, 34, 45, 55, 65, 76, 88, 100, 112, 128 };

char **directory_path;
struct dirent ***directory_list;
int *directory_list_size;
int *directory_list_position;

// Mutex for access to directory variables.
pthread_mutex_t *directory_mutex;

pthread_t next_track_pthread;

// The playlist (not including the track currently playing).
struct playlist_t **playlist;

// Mutex for access to the playlist.
pthread_mutex_t *playlist_mutex;

// Mutex signalling that the next track in the queue should be played.
pthread_mutex_t *next_track_mutex;

// SDL_mixer data for the track currently playing.
Mix_Music *mus;

// The current volume.
int volume;

// Global variable containing last button press.
enum button_press_t *button_press;

// Mutex for access to global button_press.
pthread_mutex_t *button_press_mutex;

// Mutex signalling that a button press has been made.
pthread_mutex_t *button_press_sig;

// Pthread monitoring button presses (possibly simulated).
pthread_t button_press_pthread;

// Current state of the player (stopped, paused, playing).
enum player_state_t *player_state;

// Current display mode.
enum mode_t *player_state_mode;

// The title of the current track (to be displayed on the now playing screen).
char **player_state_title;

// The current position in the track.
int *player_state_position;

// The current position in the track in seconds.
int *player_state_position_seconds;

// Scroll position for long titles.
int *player_state_scroll_pos;

// Mutex for access to player_state and player_state_title.
pthread_mutex_t *player_state_mutex;

// Signal for the screen to redraw
pthread_mutex_t *redraw_sig;

// Thread to redraw the screen when redraw_sig is signalled.
pthread_t redraw_pthread;

// Thread to scroll text too big to fit on the screen.
pthread_t scroll_pthread;

void music_length_callback(void *udata, Uint8 *stream, int len)
{
    pthread_mutex_lock(player_state_mutex);
    if(Mix_PlayingMusic() && !Mix_PausedMusic()) *player_state_position += len;
    // Divide by 4 because len is the number of bytes, there are two bytes per
    // sample per channel and two channels.
    if(*player_state_position_seconds !=
            (*player_state_position/PLAY_SAMPLERATE) / 4)
    {
        *player_state_position_seconds =
            (*player_state_position/PLAY_SAMPLERATE) / 4;
        pthread_mutex_unlock(redraw_sig);
    }
    pthread_mutex_unlock(player_state_mutex);
}

void *redraw_thread(void* v)
{
    while (1)
    {
        pthread_mutex_lock(redraw_sig);
        // Redraw the screen
        pthread_mutex_lock(player_state_mutex);
        enum mode_t mode = *player_state_mode;
        pthread_mutex_unlock(player_state_mutex);
        switch (mode)
        {
            case NOW:
            draw_now_playing();
            break;
            case VOL:
            draw_vol();
            break;
            case FILES:
            draw_directory_list();
            break;
        }
#ifdef SIMULATE_LCD
        SDL_Delay(30);
#else
        DELAY_MILLIS(30);
#endif
    }
}

void play_music(const char* path)
{
    Mix_HaltMusic();
    if(mus) Mix_FreeMusic(mus);
    mus = 0;

    // Load the next track
    mus = Mix_LoadMUS(path);
    if(!mus)
    {
        fprintf(stderr, "Could not load audio: %s\n", path);
        return;
    }

    fprintf(stderr, "Playing track: %s\n", path);
    Mix_PlayMusic(mus, 0);
}

void queue_next()
{
    pthread_mutex_lock(playlist_mutex);
    Mix_HaltMusic();
    // If there are no more tracks to play, set the player state to STOPPED.
    if(!(*playlist))
    {
        fprintf(stderr, "No more tracks in playlist\n");
        // Change the player state.
        pthread_mutex_lock(player_state_mutex);
        *player_state = STOPPED;
        free(*player_state_title);
        *player_state_title = 0;
        pthread_mutex_unlock(player_state_mutex);
    }
    // Queue the next track.
    else if((*playlist))
    {
        // Send the track to SDL_mixer.
        fprintf(stderr, "Playing %s\n", (*playlist)->path);
        play_music((*playlist)->path);
        // Note the current state.
        pthread_mutex_lock(player_state_mutex);
        if(*player_state_title != 0) free(*player_state_title);
        *player_state_title = strdup((*playlist)->title);
        *player_state = PLAYING;
        // Reset the timer.
        *player_state_position = 0;
        *player_state_position_seconds = 0;
        pthread_mutex_unlock(player_state_mutex);
        // Remove this song from the playlist.
        struct playlist_t *n2 = (*playlist)->next;
        free((*playlist)->path);
        free((*playlist)->title);
        free((*playlist));
        (*playlist) = n2;
    }

    pthread_mutex_unlock(playlist_mutex);
}

void *next_track_thread(void *v)
{
    while (1)
    {
        // Wait for a new track to become available
        pthread_mutex_lock(next_track_mutex);
        // Queue the track
        fprintf(stderr, "Queue a track\n");
        queue_next();
    }
}

void continue_queue()
{
    fprintf(stderr, "continue_queue\n");
    pthread_mutex_unlock(next_track_mutex);
}

void append_to_playlist(const char* path, const char *title)
{
    struct playlist_t *n = malloc(sizeof(struct playlist_t));
    n->path = strdup(path);
    n->title = strdup(title);
    n->next = 0;

    if(*playlist)
    {
        fprintf(stderr, "Append %s to playlist\n", path);
        struct playlist_t *head = *playlist;
        while (head->next) head = head->next;
        head->next = n;
    } else {
        *playlist = n;
        fprintf(stderr, "New playlist: Append %s to playlist\n", path);
    }
}

int wdstat(const char* filename, struct stat *buf)
{
    if(filename == 0 || buf == 0)
    {
        return -1;
    }
    char *path =
        malloc(strlen(*directory_path) + strlen(filename) + 2);
    sprintf(path, "%s/%s", *directory_path, filename);
    int s = stat(path, buf);
    free(path);
    return s;
}

void free_directory_list()
{
    pthread_mutex_lock(directory_mutex);
    if(*directory_path)
    {
        free(*directory_path);
        *directory_path = 0;
    }
    if(*directory_list)
    {
        int i = 0;
        for (i = 0; i < *directory_list_size; i++)
        {
            free((*directory_list)[i]);
        }
        free(*directory_list);
        *directory_list = 0;
        *directory_list_size = -1;
    }
    pthread_mutex_unlock(directory_mutex);
}

int is_mp3(const struct dirent *d1)
{
    struct stat d1_stat;
    wdstat(d1->d_name, &d1_stat);

    return (S_ISREG(d1_stat.st_mode) && strlen(d1->d_name) >= 4 &&
        (strcmp((char*)(d1->d_name + strlen(d1->d_name) - 4), ".mp3") == 0 ||
        strcmp((char*)(d1->d_name + strlen(d1->d_name) - 4), ".ogg") == 0)
        )?1:0;
}

int is_mp3_or_dir(const struct dirent *d1)
{
    struct stat d1_stat;
    wdstat(d1->d_name, &d1_stat);

    if(S_ISDIR(d1_stat.st_mode) &&
        (d1->d_name[0] != '.' ||
        (strcmp(d1->d_name, "..") == 0 || strcmp(d1->d_name, ".") == 0)))
        return 1;

    return (S_ISREG(d1_stat.st_mode) && strlen(d1->d_name) >= 4 &&
        (strcmp((char*)(d1->d_name + strlen(d1->d_name) - 4), ".mp3") == 0 ||
        strcmp((char*)(d1->d_name + strlen(d1->d_name) - 4), ".ogg") == 0)
        )?1:0;
}

int dirname_cmp(const struct dirent **d1, const struct dirent **d2)
{
    struct stat d1_stat, d2_stat;
    wdstat((*d1)->d_name, &d1_stat);
    wdstat((*d2)->d_name, &d2_stat);

    if( (S_ISDIR(d1_stat.st_mode) && S_ISDIR(d2_stat.st_mode))
      || (S_ISREG(d1_stat.st_mode) && S_ISREG(d2_stat.st_mode)) )
    {
        return strcmp((*d1)->d_name, (*d2)->d_name);
    } else
    if(S_ISDIR(d1_stat.st_mode) && S_ISREG(d2_stat.st_mode))
    {
        return -1;
    } else
    if(S_ISREG(d2_stat.st_mode) && S_ISDIR(d2_stat.st_mode))
    {
        return 1;
    }
}

void change_directory(const char* directory)
{
    free_directory_list();

    pthread_mutex_lock(directory_mutex);
    *directory_path = strdup(directory);
    *directory_list_position = 0;

    int n = scandir(directory, directory_list, &is_mp3_or_dir, &dirname_cmp);

    if(n < 0)
    {
        pthread_mutex_unlock(directory_mutex);
        return;
    }

    *directory_list_size = n;

    pthread_mutex_unlock(directory_mutex);
}

void wd_change_directory(const char* directory)
{
    // Construct the name of the directory to change to.
    pthread_mutex_lock(directory_mutex);
    char *current_directory = strdup(*directory_path);
    pthread_mutex_unlock(directory_mutex);

    char *new_directory = 0;
    if(strcmp(directory, ".") == 0) new_directory = strdup(current_directory);
    else if(strcmp(directory, "..") == 0)
    {
        // Move to parent directory.
        // Cannot move above the process' working directory because all paths
        // are relative to the process' working directory.
        int i = 0;
        // Remove the last "/part" from the current directory, or set it to
        // "." ifthere is no '/' ("." is in current_directory).
        for (i = strlen(current_directory); i >= 0; i--)
        {
            if(current_directory[i] == '/') break;
        }
        current_directory[i] = 0;
        new_directory = strdup(current_directory);
    } else {
        // Append "/new_dir" to the current path.
        new_directory = malloc(strlen(current_directory) +
                strlen(directory) + 2); // +2 for / and null terminator.
        snprintf(new_directory,
            strlen(current_directory) + strlen(directory) + 2,
            "%s/%s", current_directory, directory);
    }
    change_directory(new_directory);
    free(current_directory);
    free(new_directory);
}

// Queue all files in the process' current working directory, starting with
// 'start'.
void wd_queue_directory(const char* start)
{
    pthread_mutex_lock(directory_mutex);
    pthread_mutex_lock(playlist_mutex);
    // Free the current playlist.
    struct playlist_t *head = *playlist;
    *playlist = 0;
    while (head)
    {
        if(head->title) free(head->title);
        if(head->path) free(head->path);
        struct playlist_t *next = head->next;
        free(head);
        head = next;
    }

    struct dirent **dlist;
    int n = scandir(*directory_path, &dlist, &is_mp3, &dirname_cmp);
    int i;
    for (i = 0; i < n; i++)
    {
        if(start && strcmp(start, dlist[i]->d_name) > 0) continue;
        char *path =
            malloc(strlen(*directory_path) + strlen(dlist[i]->d_name) + 2);
        sprintf(path, "%s/%s", *directory_path, dlist[i]->d_name);
        append_to_playlist(path, dlist[i]->d_name);
        free(path);
        free(dlist[i]);
    }
    free(dlist);
    pthread_mutex_unlock(playlist_mutex);
    pthread_mutex_unlock(directory_mutex);
}

void draw_directory_list()
{
    pthread_mutex_lock(directory_mutex);
    char s1[lcd_width() + 1], s2[lcd_width() + 1], s3[lcd_width() + 1];
    // directory_list_position, directory_list_size
    if( (*directory_list_position - 1) >= 0 && (*directory_list_position - 1)
        < *directory_list_size )
    {
        // Draw s1
        snprintf((char*)&s1, lcd_width() + 1, " %s", (*directory_list)[*directory_list_position - 1]->d_name);
    } else s1[0] = 0;
    if( *directory_list_position >= 0 && *directory_list_position <
        *directory_list_size )
    {
        // Draw s2
        pthread_mutex_lock(player_state_mutex);
        char *title = scroll_text(
            (*directory_list)[*directory_list_position]->d_name, lcd_width() - 1, *player_state_scroll_pos);
        pthread_mutex_unlock(player_state_mutex);
        snprintf((char*)&s2, lcd_width() + 1, "-%s", title);
        if(title) free(title);
    } else s2[0] = 0;
    if( (*directory_list_position + 1) >= 0 && (*directory_list_position + 1)
        < *directory_list_size )
    {
         // Draw s3
        snprintf((char*)&s3, lcd_width() + 1, " %s", (*directory_list)[*directory_list_position + 1]->d_name);
    } else s3[0] = 0;
    pthread_mutex_unlock(directory_mutex);

    char title_line[lcd_width() + 1];
    char *t = position_string();
    snprintf((char*)&title_line, 21, "Files%-*s%s",
        lcd_width() - POSITION_STRING_LEN - 5, "", t);
    free(t);
    lcd_4line((char*)&title_line, (char*)&s1, (char*)&s2, (char*)&s3);
    lcd_2line((char*)&s1, (char*)&s2);
}

void move_list(int rel)
{
    pthread_mutex_lock(directory_mutex);
    if(*directory_list_position + rel >= 0 &&
        *directory_list_position + rel < *directory_list_size)
    {
        *directory_list_position += rel;
    }
    pthread_mutex_unlock(directory_mutex);
    pthread_mutex_lock(player_state_mutex);
    *player_state_scroll_pos = 0;
    pthread_mutex_unlock(player_state_mutex);
    pthread_mutex_unlock(redraw_sig);
}

void move_list_up()
{
    move_list(-1);
}

void move_list_down()
{
    move_list(1);
}

void *scroll_thread(void *v)
{
    while (1)
    {
        SDL_Delay(350);
        pthread_mutex_lock(player_state_mutex);
        (*player_state_scroll_pos)++;
        pthread_mutex_unlock(player_state_mutex);
        pthread_mutex_unlock(redraw_sig);
    }
}

char *position_string()
{
    pthread_mutex_lock(player_state_mutex);
    char *s = malloc(6);
    if(*player_state == PLAYING || *player_state == PAUSED)
    {
        snprintf((char*)s, 6, "%02d:%02d",
            *player_state_position_seconds / 60,
            *player_state_position_seconds % 60);
    } else {
        snprintf((char*)s, 6, "--:--");
    }
    pthread_mutex_unlock(player_state_mutex);
    return s;
}

char *scroll_text(const char* text, int length, int scroll_pos)
{
    if(!text)
        return strdup("");
    char *title = 0;
    int tlen = strlen(text);
    // The number of scroll positions that should be shown for this string.
    // The text is stationary for the first five and last five steps to make it
    // easier to read.
    int steps = tlen - length + 10;
    if(
            tlen > lcd_width() &&
            // Only scroll after the first five steps.
            (scroll_pos % steps) > 5
            )
    {
        title = strdup(
            text +
            (
                ((scroll_pos - 5) % steps > (tlen-length))?
                    (tlen - length):
                    ((scroll_pos - 5) % steps)
            )
            );
    } else {
        title = strdup(text);
    }
    return title;
}

void draw_now_playing()
{
    char *position = position_string();
    char status_string[lcd_width() + 1];
    pthread_mutex_lock(player_state_mutex);
    int state = *player_state;
    char *title = scroll_text(
            *player_state_title,
            lcd_width(),
            *player_state_scroll_pos
            );
    pthread_mutex_unlock(player_state_mutex);
    const char *play_status = 0;
    switch(state)
    {
    case PAUSED:
        play_status = "PAUSED";
        break;
    case PLAYING:
        play_status = "PLAYING";
        break;
    case STOPPED:
    default:
        play_status = "NO FILE";
        break;
    }
    snprintf(
        (char*)&status_string,
        lcd_width() + 1,
        "%-*s%s",
        lcd_width() - POSITION_STRING_LEN,
        play_status,
        position
        );
    switch (state)
    {
        case STOPPED:
        lcd_4line(
                "    Now Playing     ",
                "                    ",
                "      NO FILE       ",
                "                    "
                );
        break;
        case PLAYING:
        lcd_4line(
                "    Now Playing     ",
                position,
                title,
                "                    "
                );
        break;
        case PAUSED:
        lcd_4line(
                "    Now Playing     ",
                position,
                title,
                "      PAUSED        "
                );
        break;
    }
    lcd_2line((char*)&status_string, title);
    free(position);
    if(title) free(title);
}

void draw_vol()
{
    // Display volume as a 'progress bar'.
    char bar[lcd_width() + 1];
    snprintf(
            (char*)&bar,
            (size_t)(lcd_width() + 1),
            "%-*s|%-*s+%-*s|",
            (lcd_width() - 16)/2,
            "",
            volume,
            "",
            13 - volume,
            ""
            );

    char title_line[lcd_width() + 1];
    char *t = position_string();
    snprintf(
            (char*)&title_line,
            lcd_width() + 1,
            "Volume%-*s%s",
            lcd_width() - POSITION_STRING_LEN - 6,
            "",
            t
            );
    free(t);
    lcd_4line((char*)&title_line, "", (char*)&bar, "");
    lcd_2line((char*)&title_line, (char*)&bar);
}

void *button_press_thread(void* v)
{
    // Poll GPIO pins.
#ifdef SIMULATE_BUTTONS
    char *buffer = 0;
    size_t s = 0;
    while (getline(&buffer, &s, stdin) >= 0)
    {
        pthread_mutex_lock(button_press_mutex);
        if(strncmp(buffer, "up", 2) == 0)
        {
            *button_press = LCD_BUTTON_VOLUP;
            pthread_mutex_unlock(button_press_sig);
        }
        if(strncmp(buffer, "down", 4) == 0)
        {
            *button_press = LCD_BUTTON_VOLDOWN;
            pthread_mutex_unlock(button_press_sig);
        }
        if(strncmp(buffer, "play", 4) == 0)
        {
            *button_press = LCD_BUTTON_PLAY;
            pthread_mutex_unlock(button_press_sig);
        }
        if(strncmp(buffer, "mode", 4) == 0)
        {
            *button_press = MODE;
            pthread_mutex_unlock(button_press_sig);
        }
        if(strncmp(buffer, "quit", 4) == 0)
        {
            *button_press = QUIT;
            pthread_mutex_unlock(button_press_sig);
        }
        pthread_mutex_unlock(button_press_mutex);
    }
#else // #ifdef SIMULATE_BUTTONS
    while (1)
    {
        DELAY_MILLIS(50);
        int b = poll_button_press();
        if(b == LCD_BUTTON_NONE) continue;
        pthread_mutex_lock(button_press_mutex);
        *button_press = b;
        if (*button_press == LCD_BUTTON_VOLUP || *button_press == LCD_BUTTON_VOLDOWN)
        {
            pthread_mutex_unlock(button_press_mutex);
            pthread_mutex_unlock(button_press_sig);
            DELAY_MILLIS(300); // Delay for multiple button presses
            continue;
        } else {
            pthread_mutex_unlock(button_press_sig);
            pthread_mutex_unlock(button_press_mutex);

            // Wait for the button to be released
            while (poll_button_press() != LCD_BUTTON_NONE)
                DELAY_MILLIS(50);
        }
    }
#endif// #ifdef SIMULATE_LCD
}

void play_init()
{
    // Initialise global variables.
    directory_path = malloc(sizeof(char*));
    *directory_path = 0;
    directory_list = malloc(sizeof(struct dirent*));
    *directory_list = 0;
    directory_list_size = malloc(sizeof(int));
    *directory_list_size = -1;
    directory_list_position = malloc(sizeof(int));
    *directory_list_position = 0;

    directory_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(directory_mutex, 0);

    Mix_Music *mus = 0;

    // Initialise playlist variables.
    playlist_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(playlist_mutex, 0);
    next_track_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(next_track_mutex, 0);
    pthread_mutex_trylock(next_track_mutex);

    playlist = malloc(sizeof(struct playlist_t*));
    *playlist = 0;

    // Initialise button press variables.
    button_press = malloc(sizeof(int));
    button_press_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(button_press_mutex, 0);
    button_press_sig = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(button_press_sig, 0);

    // Initialise player_state.
    player_state = malloc(sizeof(enum player_state_t));
    *player_state = STOPPED;
    player_state_title = malloc(sizeof(char*));
    *player_state_title = 0;
    player_state_position = malloc(sizeof(int));
    *player_state_position = 0;
    player_state_position_seconds = malloc(sizeof(int));
    *player_state_position_seconds = 0;
    player_state_scroll_pos = malloc(sizeof(int));
    *player_state_scroll_pos = 0;
    player_state_mode = malloc(sizeof(enum mode_t));
    *player_state_mode = FILES;
    player_state_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(player_state_mutex, 0);

    // Initialise screen redraw signal.
    redraw_sig = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(redraw_sig, 0);
    pthread_mutex_trylock(redraw_sig);

    // Start button press thread.
    pthread_create(&button_press_pthread, 0, &button_press_thread, 0);

    // Start screen redraw thread.
    pthread_create(&redraw_pthread, 0, &redraw_thread, 0);

    // Start scrolling thread.
    pthread_create(&scroll_pthread, 0, &scroll_thread, 0);

    if(SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "Could not initialise SDL\n");
        exit(1);
    }
    int flags = MIX_INIT_MP3 & MIX_INIT_OGG;
    if(Mix_Init(flags) & flags != flags)
    {
        fprintf(stderr, "Could not initialise SDL mixer\n");
        exit(1);
    }

    Mix_SetPostMix(&music_length_callback, 0);

    if(Mix_OpenAudio(PLAY_SAMPLERATE, MIX_DEFAULT_FORMAT, 2, 1024) != 0)
    {
        fprintf(stderr, "Could not open audio\n");
        exit(1);
    }

    volume = 7;
    Mix_VolumeMusic(volume_level[volume]);

    // Queue the next track when a track finishes.
    Mix_HookMusicFinished(&continue_queue);

    pthread_create(&next_track_pthread, 0, &next_track_thread, 0);
}

void change_mode(enum mode_t mode)
{
    pthread_mutex_lock(player_state_mutex);
    *player_state_mode = mode;
    pthread_mutex_unlock(player_state_mutex);
    pthread_mutex_unlock(redraw_sig);
}

void button_press_files(enum button_press_t button)
{
    switch (*button_press)
    {
        case QUIT: break;
        case LCD_BUTTON_VOLUP:
        move_list_up();
        break;
        case LCD_BUTTON_VOLDOWN:
        move_list_down();
        break;
        case LCD_BUTTON_PLAY:
        // If directory_list_position refers to a directory, enter
        // the directory.  If it is an audio file, replace the
        // playlist with the audio files in the current list
        // starting at directory_list_position.
        if (*directory_list && *directory_list_position >= 0)
        {
            pthread_mutex_lock(directory_mutex);
            struct stat buf;
            wdstat((*directory_list)[*directory_list_position]->d_name, &buf);
            char *new_directory = strdup((*directory_list)
                [*directory_list_position]->d_name);
            pthread_mutex_unlock(directory_mutex);
            if (S_ISDIR(buf.st_mode))
            {
                // If the directory is "." (current directory),
                // queue all songs in the directory instead.
                if (strcmp(new_directory, ".") == 0)
                {
                    // Queue all songs in this directory.
                    wd_queue_directory(0);
                    // New songs queued; play the next track.
                    continue_queue();
                    change_mode(NOW);
                } else {
                    // Enter the directory.
                    wd_change_directory(new_directory);
                }
            } else if (S_ISREG(buf.st_mode))
            {
                Mix_HaltMusic();
                pthread_mutex_lock(player_state_mutex);
                *player_state = STOPPED;
                pthread_mutex_unlock(player_state_mutex);
                // Queue the directory starting at 'start'.
                wd_queue_directory(new_directory);
                // New songs queued; play the next track.
                continue_queue();
                change_mode(NOW);
            }
            pthread_mutex_unlock(redraw_sig);
            free(new_directory);
        }
    }
}

void button_press_now(enum button_press_t button)
{
    switch (*button_press)
    {
        case QUIT: break;
        case UP:
        // Skip forward ten seconds.
        pthread_mutex_lock(player_state_mutex);
        int setpos = 10 * PLAY_SAMPLERATE * 4;
        // If this is an OGG track, Mix_SetMusicPosition takes an
        // absolute position, not relative.
        if(Mix_GetMusicType(mus) == MUS_OGG)
            setpos += *player_state_position_seconds;
        int track_ended = 0;
        // Current time, use to set position
        int seconds = *player_state_position_seconds;
        pthread_mutex_unlock(player_state_mutex);

        /*if(Mix_GetMusicType(mus) == MUS_OGG) seconds += 10;
        if(Mix_GetMusicType(mus) == MUS_MP3) seconds = 10;*/
        seconds += 10;

        if(Mix_SetMusicPosition(seconds) == 0)
        {
            pthread_mutex_lock(player_state_mutex);
            *player_state_position += 10 * PLAY_SAMPLERATE * 4;
            *player_state_position_seconds += 10;
            pthread_mutex_unlock(player_state_mutex);
        } else {
            pthread_mutex_lock(player_state_mutex);
            *player_state_position = 0;
            *player_state_position_seconds = 0;
            Mix_HaltMusic();
            track_ended = 1;
            pthread_mutex_unlock(player_state_mutex);
        }
        if(track_ended) continue_queue();
        pthread_mutex_unlock(redraw_sig);
        break;
        case DOWN: {
        // Skip back ten seconds (or to the start of the track).
        // Simpler than skipping forward because we know that
        // current - 10s is inside the track.
        pthread_mutex_lock(player_state_mutex);
        int seconds = *player_state_position_seconds;
        pthread_mutex_unlock(player_state_mutex);
        Mix_RewindMusic();
        if(seconds >= 10 &&
            Mix_SetMusicPosition(
                seconds - 10) == 0)
        {
            pthread_mutex_lock(player_state_mutex);
            *player_state_position -= 10 * PLAY_SAMPLERATE * 4;
            *player_state_position_seconds -= 10;
            pthread_mutex_unlock(player_state_mutex);
        } else {
            pthread_mutex_lock(player_state_mutex);
            *player_state_position = 0;
            *player_state_position_seconds = 0;
            pthread_mutex_unlock(player_state_mutex);
        }
        break; }
        case PLAY:
        pthread_mutex_lock(player_state_mutex);
        switch (*player_state)
        {
            case PAUSED:
            // There is music loaded into SDL_mixer, start playing.
            fprintf(stderr, "Resume\n");
            Mix_ResumeMusic();
            *player_state = PLAYING;
            break;
            case PLAYING:
            // Pause the music currently playing.
            fprintf(stderr, "Pause\n");
            Mix_PauseMusic();
            *player_state = PAUSED;
            break;
        }
        pthread_mutex_unlock(player_state_mutex);
        pthread_mutex_unlock(redraw_sig);
        break;
    }
}

void button_press_vol(enum button_press_t button)
{
    switch (*button_press)
    {
        case QUIT: break;
        case LCD_BUTTON_PLAY:
        pthread_mutex_lock(player_state_mutex);
        switch (*player_state)
        {
           case PAUSED:
            // There is music loaded into SDL_mixer, start playing.
            fprintf(stderr, "Resume\n");
            Mix_ResumeMusic();
            *player_state = PLAYING;
            break;
            case PLAYING:
            // Pause the music currently playing.
            fprintf(stderr, "Pause\n");
            Mix_PauseMusic();
            *player_state = PAUSED;
            break;
        }
        pthread_mutex_unlock(player_state_mutex);
        pthread_mutex_unlock(redraw_sig);
        break;
        case LCD_BUTTON_VOLUP:
        change_mode(VOL);
        if (volume < 13)
        {
            volume++;
            Mix_VolumeMusic(volume_level[volume]);
            fprintf(stderr, "New volume: %d\n", volume);
        }
        pthread_mutex_unlock(redraw_sig);
        break;
        case LCD_BUTTON_VOLDOWN:
        change_mode(VOL);
        if (volume >= 1)
        {
            volume--;
            Mix_VolumeMusic(volume_level[volume]);
            fprintf(stderr, "New volume: %d\n", volume);
        }
        pthread_mutex_unlock(redraw_sig);
        break;
    }
}

int main(int argc, char* argv[])
{
    // Initialise the LCD.
    if (lcd_init(LCD_BUTTON_PLAY_LCD_TYPE) != 0)
    {
        fprintf(stderr, "Error initialising LCD\n");
        return 1;
    }
    // Initialise the music player.
    play_init();

    // Change the process' current working directory.
    if(argc > 1)
    {
        chdir(argv[1]);
    }

    change_directory(".");
    pthread_mutex_unlock(redraw_sig);

    // Start playing from the queue.
    pthread_mutex_unlock(next_track_mutex);

    // Print the queue.
    pthread_mutex_lock(playlist_mutex);
    struct playlist_t *head = *playlist;
    while (head)
    {
        fprintf(stderr, "Track: %s, %s\n", head->title, head->path);
        head = head->next;
    }
    pthread_mutex_unlock(playlist_mutex);

    int quit = 0;
    while (!quit)
    {
        // Pretend to poll the bcm2835 pins.
        int l = pthread_mutex_trylock(button_press_sig);
        if(l == 0)
        {
            // A new button press is available.
            pthread_mutex_lock(button_press_mutex);
            // Apply the button press.
            if(*button_press == QUIT) quit = 1;

            pthread_mutex_lock(player_state_mutex);
            enum mode_t mode = *player_state_mode;
            pthread_mutex_unlock(player_state_mutex);
            // Context-free buttons (RW, FF, FILE, VOL) are processed in the
            // same way for all modes.
            switch (*button_press)
            {
            case LCD_BUTTON_FF:
                // Skip forward ten seconds.
                pthread_mutex_lock(player_state_mutex);
                int setpos = 10 * PLAY_SAMPLERATE * 4;
                // If this is an OGG track, Mix_SetMusicPosition takes an
                // absolute position, not relative.
                if (Mix_GetMusicType(mus) == MUS_OGG)
                    setpos += *player_state_position_seconds;
                int track_ended = 0;
                // Current time, use to set position
                int seconds = *player_state_position_seconds;
                pthread_mutex_unlock(player_state_mutex);

                /*if (Mix_GetMusicType(mus) == MUS_OGG) seconds += 10;
                if (Mix_GetMusicType(mus) == MUS_MP3) seconds = 10;*/
                seconds += 10;

                if (Mix_SetMusicPosition(seconds) == 0)
                {
                    pthread_mutex_lock(player_state_mutex);
                    *player_state_position += 10 * PLAY_SAMPLERATE * 4;
                    *player_state_position_seconds += 10;
                    pthread_mutex_unlock(player_state_mutex);
                } else {
                    pthread_mutex_lock(player_state_mutex);
                    *player_state_position = 0;
                    *player_state_position_seconds = 0;
                    Mix_HaltMusic();
                    track_ended = 1;
                    pthread_mutex_unlock(player_state_mutex);
                }
                if (track_ended) continue_queue();
                pthread_mutex_unlock(redraw_sig);
                break;
            case LCD_BUTTON_RW: {
                // Skip back ten seconds (or to the start of the track).
                // Simpler than skipping forward because we know that
                // current - 10s is inside the track.
                pthread_mutex_lock(player_state_mutex);
                int seconds = *player_state_position_seconds;
                pthread_mutex_unlock(player_state_mutex);
                Mix_RewindMusic();
                if (seconds >= 10 &&
                    Mix_SetMusicPosition(
                        seconds - 10) == 0)
                {
                    pthread_mutex_lock(player_state_mutex);
                    *player_state_position -= 10 * PLAY_SAMPLERATE * 4;
                    *player_state_position_seconds -= 10;
                    pthread_mutex_unlock(player_state_mutex);
                } else {
                    pthread_mutex_lock(player_state_mutex);
                    *player_state_position = 0;
                    *player_state_position_seconds = 0;
                    pthread_mutex_unlock(player_state_mutex);
                }
                break; }
            case LCD_BUTTON_FILE:
                change_mode(FILES);
                break;
            case LCD_BUTTON_VOL:
                change_mode(VOL);
                break;
            case LCD_BUTTON_NOW:
                change_mode(NOW);
                break;
            }

            switch (mode)
            {
            case FILES:
                button_press_files(*button_press);
                break;
            case NOW:
                button_press_now(*button_press);
                break;
            case VOL:
                button_press_vol(*button_press);
                break;
            }
            pthread_mutex_unlock(button_press_mutex);
        }
        SDL_Delay(50);
    }

    // Shut down SDL_mixer and SDL.
    Mix_HaltMusic();
    if(mus) Mix_FreeMusic(mus);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    lcd_close();
}

