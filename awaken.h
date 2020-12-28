#ifndef AWAKEN_H
#define AWAKEN_H

#include "linuxrec.h"

#define E_SR_NOACTIVEDEVICE		1
#define E_SR_NOMEM				2
#define E_SR_INVAL				3
#define E_SR_RECORDFAIL			4
#define E_SR_ALREADY			5

typedef struct{
	void (*on_result)(const char *result, char is_last);
	void (*on_awaken_begin)();
	void (*on_awaken_end)(int reason);	/* 0 if VAD.  others, error : see E_SR_xxx and msp_errors.h  */
} awaken_rec_notifier;

typedef struct{
    awaken_rec_notifier notif;
    const char *session_id;
    struct recorder *recorder;
    char *session_begin_params;
    int state;
    int audio_status;
}awaken_rec;

int ak_init(awaken_rec *ar, const char *session_begin_params,awaken_rec_notifier *notify);
int ak_starting_listening(awaken_rec *ar);
int ak_stop_listening(awaken_rec *ar);
void ak_uninit(awaken_rec *ar);
#endif
