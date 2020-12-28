#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "awaken.h"
#include "msp_errors.h"
#include "msp_cmn.h"
#include "qivw.h"

int main(int argc, char *argv[])
{

	int         ret       = MSP_SUCCESS;
	const char *lgi_param = "appid = 5fc4a959,work_dir = .";
	const char *ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";
	int errcode;
	int i = 0;
	awaken_rec iat;
    awaken_rec_notifier notify = {
        NULL, NULL, NULL
    };

	ret = MSPLogin(NULL, NULL, lgi_param);
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed, error code: %d.\n", ret);
		goto exit ;//登录失败，退出登录
	}

	errcode = ak_init(&iat, ssb_param, &notify);
	if (errcode) {
		printf("speech recognizer init failed\n");
		return errcode;
	}
	errcode = ak_starting_listening(&iat);
	if (errcode) {
		printf("start listen failed %d\n", errcode);
	}
	/* demo 15 seconds recording */
	while(i++ < 15)
		sleep(1);
	errcode = ak_stop_listening(&iat);
	if (errcode) {
		printf("stop listening failed %d\n", errcode);
	}

    ak_uninit(&iat);

exit:
	MSPLogout(); //退出登录
	return 0;

}
