#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <time.h>
#include "awaken.h"
#include "msp_errors.h"
#include "msp_cmn.h"
#include "qivw.h"
#include "qisr.h"
#include "speech_recognizer.h"
#include "sound_playback.h"

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
   ,XIUXIU_STATUS_RECOGNIZED
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

extern int text_to_speech(const char* text);


int cb_ivw_msg_proc( const char *sessionID, int msg, int param1, int param2, const void *info, void *userData )
{

	if(MSP_IVW_MSG_ERROR == msg){
		dbg("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
        return -1;
	}else if (MSP_IVW_MSG_WAKEUP == msg){
        dbg("wake up\n");
        g_status = XIUXIU_STATUS_AWAKEN;
	}
	return 0;
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
				dbg("mem alloc failed\n");
				return;
			}
		}
		strncat(g_result, result, size);
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

	dbg("Start Listening...\n");
}
void on_speech_end(int reason)
{
	if (reason == 0){
		dbg("\nSpeaking done \n");
        dbg("Result:%s\n", g_result);
        g_status = XIUXIU_STATUS_RECOGNIZED;
    }
	else
		dbg("\nRecognizer error %d\n", reason);
}

int build_grm_cb(int ecode, const char *info, void *udata)
{
	UserData *grm_data = (UserData *)udata;

	if (NULL != grm_data) {
		grm_data->build_fini = 1;
		grm_data->errcode = ecode;
	}

	if (MSP_SUCCESS == ecode && NULL != info) {
		dbg("构建语法成功！ 语法ID:%s\n", info);
		if (NULL != grm_data)
			snprintf(grm_data->grammar_id, MAX_GRAMMARID_LEN - 1, info);
	}
	else
		dbg("构建语法失败！%d\n", ecode);

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
		dbg("打开\"%s\"文件失败！[%s]\n", GRM_FILE, strerror(errno));
		return -1; 
	}

	fseek(grm_file, 0, SEEK_END);
	grm_cnt_len = ftell(grm_file);
	fseek(grm_file, 0, SEEK_SET);

	grm_content = (char *)malloc(grm_cnt_len + 1);
	if (NULL == grm_content)
	{
		dbg("内存分配失败!\n");
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

static void print_elements(xmlNode *a_node){

    xmlNode *cur_node = NULL;

    for(cur_node = a_node; cur_node; cur_node = cur_node->next){
        if(cur_node->type == XML_ELEMENT_NODE){
            dbg("node type: Element, name:%s\n", cur_node->name);
        }
        if(cur_node->type == XML_TEXT_NODE){
            dbg("node type: Text, content:%s\n", cur_node->content);
        }
        print_elements(cur_node->children);
    }
}

static xmlChar* get_element_content(xmlNode *root, const char* node_name){

    xmlNode *cur_node;
    xmlChar *content;

    for(cur_node = root; cur_node; cur_node = cur_node->next){
        if(cur_node->type == XML_ELEMENT_NODE && strcmp(cur_node->name, node_name) == 0){
            if(cur_node->children){
                return cur_node->children->content;
            }
        }
        if(content = get_element_content(cur_node->children, node_name))
            return content;
    }

    return NULL;
}

static void random_init(){

    struct timespec ttime = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ttime);
    unsigned int seed = ttime.tv_sec+ttime.tv_nsec;
    srandom(seed);
}

static int random_constructor(int num){

    random_init();
    return num*random()/RAND_MAX;
}

void not_recognized(){

    int ret;

    const char *asorry[2] = {"对不起", "不好意思"};
    const char *aplease[2] = {"麻烦", "请"};
    const char *not_catched = "我没有听清";
    const char *say_again = "你再说一遍";

    const char* sorry = asorry[random_constructor(2)];
    const char* please = aplease[random_constructor(2)];

    char response[200];
    response[0] = '\0';
    strcat(strcat(strcat(strcat(response, sorry), not_catched), please), say_again) ;
    ret = text_to_speech(response);
    if(MSP_SUCCESS != ret){
        dbg("text to speech failed:%d", ret);
        return;
    }
    audio_play("tmp.wav", 0);
}

void greeting(){

    int ret;

    ret = text_to_speech("你好");
    if(MSP_SUCCESS != ret){
        dbg("text to speech failed:%d", ret);
        return;
    }
    audio_play("tmp.wav", 0);
}

void cmd_pro(){

    xmlDocPtr doc = NULL;
    xmlNode *root;
    char response[200];
    response[0] = '\0';
    int ret;
    int success = 1;

    if(!g_result || *g_result == 0){
        success = 0;
        goto exit;
    }
    doc = xmlReadMemory(g_result, strlen(g_result), "noname.xml", NULL, 0);
    if(doc == NULL){
        dbg("Failed to parse document\n");
        success = 0;
        goto exit;
    }
    root = xmlDocGetRootElement(doc);
    xmlChar *confidence = get_element_content(root, "confidence");
    if(!confidence){
        dbg("Error:no confidence:%s\n", confidence);
        success = 0;
        goto exit;
    }
    int confidence_i = atoi(confidence);
    if(confidence_i == 0){
        ferror("Convert confidence to integer failed\n");
        success = 0;
        goto exit;
    }

    if(confidence_i < 20){
        dbg("Confidence smallar than 50.\n");
        success = 0;
        goto exit;
    }
    xmlChar *something = get_element_content(root, "something");
    if(!something){
        xmlChar *time = get_element_content(root, "time");
        if(time && (strcmp(time, "下一首") == 0 || strcmp(time, "上一首") == 0)){
            /*! TODO: play music
             */
            if(strcmp(time, "下一首") == 0){
            }else{
            }
        }else{
            success = 0;
        }
        goto exit;
    }

    xmlChar *dopre = get_element_content(root, "dopre");
    if(dopre){
        /*operation-command*/
        if(strcmp(something, "最大能量") == 0
                || strcmp(something, "能量") == 0
                || strcmp(something, "温度") == 0
                || strcmp(something, "温") == 0){
            strcat(response, "能量已");
        }else if(strcmp(something, "力度") == 0){
            strcat(response, "力度已");
        }else if(strcmp(something, "歌") == 0
                || strcmp(something, "歌曲") == 0
                || strcmp(something, "音乐") == 0){
            if(strcmp(dopre, "播放") == 0 
                    || strcmp(dopre, "播") == 0){
            }else if(strcmp(dopre, "停止") == 0
                    || strcmp(dopre, "暂停") == 0
                    || strcmp(dopre, "停止播放") == 0
                    || strcmp(dopre, "暂停播放") == 0){
            }else{
                dbg("Error:Unexpected grammer:%s\n", dopre);
                success = 0;
            }
            /*! TODO: play music
             */
            goto exit;
        }else{
            dbg("Error:Unexpected grammer:%s\n", something);
            success = 0;
            goto exit;
        }
        if(strcmp(dopre, "增") == 0
                || strcmp(dopre, "增加") == 0
                || strcmp(dopre, "提高") == 0
                || strcmp(dopre, "升") == 0
                || strcmp(dopre, "加大") == 0){
            strcat(response, "增加");
        }else if(strcmp(dopre, "降低") == 0
                || strcmp(dopre, "降") == 0
                || strcmp(dopre, "减") == 0
                || strcmp(dopre, "减少") == 0
                || strcmp(dopre, "减小") == 0){
            strcat(response, "减小");
        }else{
            dbg("Error:Unexpected grammer:%s\n", dopre);
            success = 0;
            goto exit;
        }
        ret = text_to_speech(response);
        if(MSP_SUCCESS != ret){
            dbg("text to speech failed:%d", ret);
            success = 0;
            goto exit;
        }
        audio_play("tmp.wav", 0);
    }else{
        /*asking-command*/
        xmlChar *value = get_element_content(root, "value");
        if(!value){
            dbg("Grammer error:no value\n");
            success = 0;
            goto exit;
        }
    }
    /*print_elements(root);*/

exit:
    if(doc)
        xmlFreeDoc(doc);
    if(success){
        g_status = XIUXIU_STATUS_INIT;
    }else{
        not_recognized();
        g_status = XIUXIU_STATUS_RECOGNIZING;
    }
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

    audio_init();

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
                g_status = XIUXIU_STATUS_RECOGNIZING;
                greeting();
                break;

            case XIUXIU_STATUS_RECOGNIZING:
                errcode = sr_start_listening(&sr_iat);
                if(errcode){
                    printf("Speech recognizer start listening failed:%d\n", errcode);
                }
                g_status = XIUXIU_STATUS_SLEEPING;
                break;

            case XIUXIU_STATUS_RECOGNIZED:
                sr_stop_listening(&sr_iat);
                cmd_pro();
                break;
        }
    }
    /*! TODO: check the status
     */
	/*errcode = ak_stop_listening(&ak_iat);*/
	/*errcode = sr_stop_listening(&sr_iat);*/
	/*if (errcode) {*/
		/*printf("stop listening failed %d\n", errcode);*/
	/*}*/

    ak_uninit(&ak_iat);
    sr_uninit(&sr_iat);

exit:
    audio_destroy();
	MSPLogout(); //退出登录
	return 0;

}
