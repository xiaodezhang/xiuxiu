#ifndef SOUND_PLAYBACK_H
#define SOUND_PLAYBACK_H

#define AUDIO_LOW_PRIORITY -1;

typedef enum{ 
    PLAY_TYPE_RANDOM = 0,
    PLAY_TYPE_SEQUENCE,
    PLAY_TYPE_SINGLE
} MUSIC_PLAY_TYPE ;

typedef void (*MUSIC_START_CALLBACK)(int current_music);
typedef void (*AUDIO_FINISHED_CALLBACK)();

typedef struct{
    char **list;
    int num;
    int current;
    MUSIC_PLAY_TYPE type;
    MUSIC_START_CALLBACK call;
}Music;

int music_init(Music *music);
int music_destory();

int music_play();
int music_pause();
int music_next();
int music_previous();
int get_current_music();
int music_play_type(MUSIC_PLAY_TYPE type);
int get_music_play_type();
int music_specify(int id);

//int update_music_list(const char** music_list, int num);

void volume_init(int volume);
void toggle_volume(int volume);

int audio_init();
int audio_play(const char *filename, int priority);
int audio_destroy();

#endif
