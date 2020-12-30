#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "awaken.h"
#include "msp_errors.h"
#include "msp_cmn.h"
#include "qivw.h"
#include "qisr.h"
#include "speech_recognizer.h"

#define	BUFFER_SIZE	4096
#define SAMPLE_RATE_16K     (16000)
#define MAX_GRAMMARID_LEN   (32)
#define MAX_PARAMS_LEN      (1024)
#define dbg printf

enum{
    XIUXIU_STATUS_INIT
   ,XIUXIU_STATUS_SLEEPING
   ,XIUXIU_STATUS_AWAKEN
   ,XIUXIU_STATUS_RECOGNIZING
};

volatile int g_status;
static char *g_result = NULL;
static unsigned int g_buffersize = BUFFER_SIZE;

const char * ASR_RES_PATH        = "fo|res/asr/common.jet"; //离线语法识别资源路径
const char * GRM_BUILD_PATH      = "res/asr/GrmBuilld"; //构建离线语法识别网络生成数据保存路径
const char * GRM_FILE            = "call.bnf"; //构建离线识别语法网络所用的语法文件
const char * LEX_NAME            = "contact"; //更新离线识别语法的contact槽（语法文件为此示例中使用的call.bnf）

typedef struct _UserData {
	int     build_fini; //标识语法构建是否完成
	int     update_fini; //标识更新词典是否完成
	int     errcode; //记录语法构建或更新词典回调错误码
	char    grammar_id[MAX_GRAMMARID_LEN]; //保存语法构建返回的语法ID
}UserData;

int cb_ivw_msg_proc( const char *sessionID, int msg, int param1, int param2, const void *info, void *userData )
{
    awaken_rec *ar = userData;

	if(MSP_IVW_MSG_ERROR == msg){
		dbg("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
        return -1;
	}else if (MSP_IVW_MSG_WAKEUP == msg){
        dbg("wake up\n");
        printf("99999999\n");
        g_status = XIUXIU_STATUS_AWAKEN;
	}
	return 0;
}

static void show_result(char *string, char is_over)
{
	printf("\rResult: [ %s ]", string);
	if(is_over)
		putchar('\n');
}

void on_result(const char *result, char is_last)
{
	if (result) {
		size_t left = g_buffersize - 1 - strlen(g_result);
		size_t size = strlen(result);
		if (left < size) {
			g_result = (char*)realloc(g_result, g_buffersize + BUFFER_SIZE);
			if (g_result)
				g_buffersize += BUFFER_SIZE;
			else {
				printf("mem alloc failed\n");
				return;
			}
		}
		strncat(g_result, result, size);
		show_result(g_result, is_last);
	}
}
void on_speech_begin()
{
	if (g_result)
	{
		free(g_result);
	}
	g_result = (char*)malloc(BUFFER_SIZE);
	g_buffersize = BUFFER_SIZE;
	memset(g_result, 0, g_buffersize);

	printf("Start Listening...\n");
}
void on_speech_end(int reason)
{
	if (reason == 0){
		printf("\nSpeaking done \n");
    }
	else
		printf("\nRecognizer error %d\n", reason);
}

int build_grm_cb(int ecode, const char *info, void *udata)
{
	UserData *grm_data = (UserData *)udata;

	if (NULL != grm_data) {
		grm_data->build_fini = 1;
		grm_data->errcode = ecode;
	}

	if (MSP_SUCCESS == ecode && NULL != info) {
		printf("构建语法成功！ 语法ID:%s\n", info);
		if (NULL != grm_data)
			snprintf(grm_data->grammar_id, MAX_GRAMMARID_LEN - 1, info);
	}
	else
		printf("构建语法失败！%d\n", ecode);

	return 0;
}

int build_grammar(UserData *udata)
{
	FILE *grm_file                           = NULL;
	char *grm_content                        = NULL;
	unsigned int grm_cnt_len                 = 0;
	char grm_build_params[MAX_PARAMS_LEN];
	int ret                                  = 0;

	grm_file = fopen(GRM_FILE, "rb");	
	if(NULL == grm_file) {
		printf("打开\"%s\"文件失败！[%s]\n", GRM_FILE, strerror(errno));
		return -1; 
	}

	fseek(grm_file, 0, SEEK_END);
	grm_cnt_len = ftell(grm_file);
	fseek(grm_file, 0, SEEK_SET);

	grm_content = (char *)malloc(grm_cnt_len + 1);
	if (NULL == grm_content)
	{
		printf("内存分配失败!\n");
		fclose(grm_file);
		grm_file = NULL;
		return -1;
	}
	fread((void*)grm_content, 1, grm_cnt_len, grm_file);
	grm_content[grm_cnt_len] = '\0';
	fclose(grm_file);
	grm_file = NULL;

	snprintf(grm_build_params, MAX_PARAMS_LEN - 1, 
		"engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, ",
		ASR_RES_PATH,
		SAMPLE_RATE_16K,
		GRM_BUILD_PATH
		);
	ret = QISRBuildGrammar("bnf", grm_content, grm_cnt_len, grm_build_params, build_grm_cb, udata);

	free(grm_content);
	grm_content = NULL;

	return ret;
}

int main(int argc, char *argv[])
{

	int         ret       = MSP_SUCCESS;
	const char *lgi_param = "appid = 5fc4a959,work_dir = .";
	const char *ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";
	char asr_params[MAX_PARAMS_LEN];
	int errcode;
	awaken_rec ak_iat;
    struct speech_rec sr_iat;
	struct speech_rec_notifier sr_notify= {
		on_result,
		on_speech_begin,
		on_speech_end
	};
    UserData asr_data;

	ret = MSPLogin(NULL, NULL, lgi_param);
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed, error code: %d.\n", ret);
		goto exit ;//登录失败，退出登录
	}

	errcode = ak_init(&ak_iat, ssb_param, cb_ivw_msg_proc);
	if (errcode != 0) {
		printf("speech recognizer init failed\n");
		return errcode;
	}

#if 1
    memset(&asr_data, 0, sizeof(UserData));
    ret = build_grammar(&asr_data);
    if(MSP_SUCCESS != ret){
        printf("build grammer failed:%d\n", ret);
        goto exit;
    }
    while(1 != asr_data.build_fini)
        usleep(300 * 1000);

    if(MSP_SUCCESS != asr_data.errcode){
        goto exit;
    }

	snprintf(asr_params, MAX_PARAMS_LEN - 1, 
		"engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, local_grammar = %s, \
		result_type = xml, result_encoding = UTF-8, ",
		ASR_RES_PATH,
		SAMPLE_RATE_16K,
		GRM_BUILD_PATH,
		asr_data.grammar_id
		);

    errcode = sr_init(&sr_iat, asr_params, &sr_notify);
    if(errcode){
        printf("speech recognizer init failed\n");
        return -1;
    }

#endif
    g_status = XIUXIU_STATUS_INIT;
    /*g_status = XIUXIU_STATUS_AWAKEN;*/
	while(1){
        switch (g_status) {
            case XIUXIU_STATUS_INIT:
                errcode = ak_starting_listening(&ak_iat);
                if (errcode) {
                    printf("Awaken start listening failed %d\n", errcode);
                }
                printf("ak start listening\n");
                g_status = XIUXIU_STATUS_SLEEPING;
                break;

            case XIUXIU_STATUS_SLEEPING:
                sleep(1);
                break;

            case XIUXIU_STATUS_AWAKEN:
                ak_stop_listening(&ak_iat);
                errcode = sr_start_listening(&sr_iat);
                if(errcode){
                    printf("Speech recognizer start listening failed:%d\n", errcode);
                }
                g_status = XIUXIU_STATUS_SLEEPING;
                break;
        }
    }
    /*! TODO: check the status
     */
	errcode = ak_stop_listening(&ak_iat);
	errcode = sr_stop_listening(&sr_iat);
	if (errcode) {
		printf("stop listening failed %d\n", errcode);
	}

    ak_uninit(&ak_iat);
    sr_uninit(&sr_iat);

exit:
	MSPLogout(); //退出登录
	return 0;

}
