#ifndef AWAKEN_H
#define AWAKEN_H

#include "linuxrec.h"

#define E_SR_NOACTIVEDEVICE		1
#define E_SR_NOMEM				2
#define E_SR_INVAL				3
#define E_SR_RECORDFAIL			4
#define E_SR_ALREADY			5

typedef int (*Ak_callback)(const char *sessionID, int msg, int param1, int param2, const void *info, void *userData);
typedef struct{
    const char *session_id;
    struct recorder *recorder;
    char *session_begin_params;
    int state;
    int audio_status;
    Ak_callback ak_callback;
}awaken_rec;

int ak_init(awaken_rec *ar, const char *session_begin_params,Ak_callback ak_callback);
int ak_starting_listening(awaken_rec *ar);
int ak_stop_listening(awaken_rec *ar);
void ak_uninit(awaken_rec *ar);
#endif
