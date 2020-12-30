#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "qivw.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "linuxrec.h"
#include "formats.h"
#include "awaken.h"

#define AK_DBGON 1
#if AK_DBGON == 1
#define dbg printf
#else
#define dbg
#endif

#define DEFAULT_FORMAT		\
{\
	WAVE_FORMAT_PCM,	\
	1,			\
	16000,			\
	32000,			\
	2,			\
	16,			\
	sizeof(WAVEFORMATEX)	\
}

enum{
    AK_STATE_INIT,
    AK_STATE_STARTED
};
extern int g_status;

static void Sleep(size_t ms)
{
	usleep(ms*1000);
}


static void iat_cb(char* data, unsigned long len, void *user_para){

    int ret;
    awaken_rec *ar = user_para;
	char sse_hints[128];

    ret = QIVWAudioWrite(ar->session_id, data, len, ar->audio_status);
    if(MSP_SUCCESS != ret){
        dbg("QIVWAudioWrite failed:%d.\n", ret);
        snprintf(sse_hints, sizeof(sse_hints), "QIVWAudioWrite errorCode=%d", ret);
        QIVWSessionEnd(ar->session_id, sse_hints);
        return;
    }
    ar->audio_status = MSP_AUDIO_SAMPLE_CONTINUE;
}

int ak_init(awaken_rec *ar, const char *session_begin_params
                , Ak_callback ak_callback){

    int errcode;
    size_t param_size;

    if(get_input_dev_num() == 0){
        return -E_SR_NOACTIVEDEVICE;
    }

    if(!ar || !session_begin_params)
        return -E_SR_INVAL;


    memset(ar, 0, sizeof(awaken_rec));
    ar->state = AK_STATE_INIT;
    ar->audio_status = MSP_AUDIO_SAMPLE_FIRST;

    param_size = strlen(session_begin_params) + 1;
    ar->session_begin_params = (char*)malloc(param_size);
	if (ar->session_begin_params == NULL) {
		dbg("mem alloc failed\n");
		return -E_SR_NOMEM;
	}
	strncpy(ar->session_begin_params, session_begin_params, param_size);
    ar->ak_callback = ak_callback;

    errcode = create_recorder(&ar->recorder, iat_cb, (void*)ar);
    if (ar->recorder == NULL || errcode != 0) {
        dbg("create recorder failed: %d\n", errcode);
        errcode = -E_SR_RECORDFAIL;
        goto fail;
    }


	return 0;

fail:
#if 0
    dbg("init faild\n");
#else
	if (ar->recorder) {
		destroy_recorder(ar->recorder);
		ar->recorder = NULL;
	}

	if (ar->session_begin_params) {
		free(ar->session_begin_params);
		ar->session_begin_params = NULL;
	}
#endif
	return errcode;
}

int ak_starting_listening(awaken_rec *ar){

    const char* session_id;
    int err_code = MSP_SUCCESS;
    int ret;
    int errcode;
	WAVEFORMATEX wavfmt = DEFAULT_FORMAT;

    if(ar->state == AK_STATE_STARTED){
        dbg("Already started.\n");
        return -E_SR_ALREADY;
    }

    session_id = QIVWSessionBegin(NULL, ar->session_begin_params, &err_code);
    if(MSP_SUCCESS != err_code){
        dbg("QIVWSessionBegin failed! error code:%d\n", err_code);
        return err_code;
    }

    err_code = QIVWRegisterNotify(session_id, ar->ak_callback, ar);
	if (err_code != MSP_SUCCESS)
	{
		dbg("QIVWRegisterNotify failed! error code:%d\n",err_code);
        QIVWSessionEnd(session_id, "QIVWRegisterNotify failed");
        return -1;
	}

    ar->audio_status = MSP_AUDIO_SAMPLE_FIRST;
    ar->session_id = session_id;

    errcode = open_recorder(ar->recorder, get_default_input_dev(), &wavfmt);
    if (errcode != 0) {
        dbg("recorder open failed: %d\n", errcode);
        QIVWSessionEnd(session_id, "open recorder failed");
        return -E_SR_RECORDFAIL;
    }

    ret = start_record(ar->recorder);
    if(ret != 0){
        dbg("start recorder failed:%d\n", ret);
        QIVWSessionEnd(session_id, "start recorder failed");
        return -E_SR_RECORDFAIL;
    }

    ar->state = AK_STATE_STARTED;

    return 0;
}

/* after stop_record, there are still some data callbacks */
static void wait_for_rec_stop(struct recorder *rec, unsigned int timeout_ms)
{
	while (!is_record_stopped(rec)) {
		Sleep(1);
		if (timeout_ms != (unsigned int)-1)
			if (0 == timeout_ms--)
				break;
	}
}

int ak_stop_listening(awaken_rec *ar){

    int ret;
	char sse_hints[128];

    if(ar->state < AK_STATE_STARTED){
        dbg("Not started or already stopped.\n");
        return 0;
    }

    ret = stop_record(ar->recorder);
    if(ret != 0){
        dbg("Stop failed!\n");
        return -E_SR_RECORDFAIL;
    }
    wait_for_rec_stop(ar->recorder, -1);
    close_recorder(ar->recorder);
    ar->state = AK_STATE_INIT;
    ret = QIVWAudioWrite(ar->session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST);
    if(MSP_SUCCESS != ret){
        dbg("QIVWAudioWrite failed:%d.\n", ret);
        snprintf(sse_hints, sizeof(sse_hints), "QIVWAudioWrite errorCode=%d", ret);
        QIVWSessionEnd(ar->session_id, sse_hints);
        return ret;
    }
    QIVWSessionEnd(ar->session_id, "sucess");
    ar->session_id = NULL;
    return 0;
}

void ak_uninit(awaken_rec *ar)
{
	if (ar->recorder) {
		if(!is_record_stopped(ar->recorder))
			stop_record(ar->recorder);
		destroy_recorder(ar->recorder);
		ar->recorder = NULL;
	}

	if (ar->session_begin_params) {
		free(ar->session_begin_params);
		ar->session_begin_params = NULL;
	}
}
