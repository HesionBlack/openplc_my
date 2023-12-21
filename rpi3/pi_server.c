/*************************************************
本服务器由此作者提供，在此基础上修改
Function:tcp 服务端进程，服务器中运行只一个
author:zyh
date:2020.4
**************************************************/
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include <stdint.h>
#include<pthread.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include"tcp_spi.h"
#include "cJSON.h"
#define BUFFER_SIZE		1024
typedef uint8_t  IEC_BOOL;
typedef uint16_t   IEC_UINT;
int ignored_bool_inputs[] = {-1};
int ignored_bool_outputs[] = {-1};
int ignored_int_inputs[] = {-1};
int ignored_int_outputs[] = {-1};
IEC_UINT int_output[BUFFER_SIZE];
#if !defined(ARRAY_SIZE)
    #define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))
#endif

#define MAX_INPUT 		14
#define MAX_OUTPUT 		11
#define MAX_ANALOG_OUT	1

/********************I/O PINS CONFIGURATION*********************
 * A good source for RaspberryPi I/O pins information is:
 * http://pinout.xyz
 *
 * The buffers below works as an internal mask, so that the
 * OpenPLC can access each pin sequentially
****************************************************************/
//inBufferPinMask: pin mask for each input, which
//means what pin is mapped to that OpenPLC input
int inBufferPinMask[MAX_INPUT] = { 8, 9, 7, 0, 2, 3, 12, 13, 14, 21, 22, 23, 24, 25 };

//outBufferPinMask: pin mask for each output, which
//means what pin is mapped to that OpenPLC output
int outBufferPinMask[MAX_OUTPUT] =	{ 15, 16, 4, 5, 6, 10, 11, 26, 27, 28, 29 };

//analogOutBufferPinMask: pin mask for the analog PWM
//output of the RaspberryPi
int analogOutBufferPinMask[MAX_ANALOG_OUT] = { 1 };
// 创建一个二维数组
int array[1024][8];
#define MAX_ROWS 1024
#define MAX_COLS 8
pthread_mutex_t bufferLock; //mutex for the internal buffers


void handleWriteAction(cJSON *root){
	printf("远端向树莓派输入数据");
    cJSON* intOutput = cJSON_GetObjectItem(root, "int_output"); 
    int_output[0] = intOutput->valueint;

    // 获取二维数组
    cJSON *matrix = cJSON_GetObjectItemCaseSensitive(root, "bool_input");
    // 假设我们知道 matrix 的大小或者我们可以动态确定它的大小
    const int rows = cJSON_GetArraySize(matrix); // 行数
    int cols = 0; // 列数，稍后确定
    int sparse[rows][3];
    // 声明并动态分配 int 类型的二维数组空间
    for (int i = 0; i < rows; i++) {
        cJSON *row = cJSON_GetArrayItem(matrix, i);
        // 遍历列
        for (int j = 0; j < 3; j++) {
            cJSON *item = cJSON_GetArrayItem(row, j);
            // 存储 int 值
            sparse[i][j] = item->valueint;
            printf("%d-",item->valueint);
        }
    }
    for (int i =1; i < rows; i++) {
        int x = sparse[0];
        int y = sparse[1];
        array[x][y]=(uint8_t)sparse[2];
    }    
    updateBuffersOut();
}

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	wiringPiSetup();
	//piHiPri(99);

	//set pins as input
	for (int i = 0; i < MAX_INPUT; i++)
	{
	    if (pinNotPresent(ignored_bool_inputs, ARRAY_SIZE(ignored_bool_inputs), inBufferPinMask[i]))
	    {
		    pinMode(inBufferPinMask[i], INPUT);
		    if (i != 0 && i != 1) //pull down can't be enabled on the first two pins
		    {
			    pullUpDnControl(inBufferPinMask[i], PUD_DOWN); //pull down enabled
		    }
	    }
	}

	//set pins as output
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
	    if (pinNotPresent(ignored_bool_outputs, ARRAY_SIZE(ignored_bool_outputs), outBufferPinMask[i]))
	    	pinMode(outBufferPinMask[i], OUTPUT);
	}

	//set PWM pins as output
	for (int i = 0; i < MAX_ANALOG_OUT; i++)
	{
	    if (pinNotPresent(ignored_int_outputs, ARRAY_SIZE(ignored_int_outputs), analogOutBufferPinMask[i]))
    		pinMode(analogOutBufferPinMask[i], PWM_OUTPUT);
	}
}
void updateBuffersIn()
{
	pthread_mutex_lock(&bufferLock); //lock mutex

	//INPUT
	for (int i = 0; i < MAX_INPUT; i++)
	{
	    if (pinNotPresent(ignored_bool_inputs, ARRAY_SIZE(ignored_bool_inputs), inBufferPinMask[i]))
    		if (array[i/8][i%8] != NULL) array[i/8][i%8] = digitalRead(inBufferPinMask[i]);
	}

	pthread_mutex_unlock(&bufferLock); //unlock mutex
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual state of the output pins. The mutex buffer_lock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersOut()
{
    printf("远端向树莓派输入数据");
	pthread_mutex_lock(&bufferLock); //lock mutex

	//OUTPUT
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
	    if (pinNotPresent(ignored_bool_outputs, ARRAY_SIZE(ignored_bool_outputs), outBufferPinMask[i]))
    		if (array[i/8][i%8] != NULL) digitalWrite(outBufferPinMask[i], array[i/8][i%8]);
	}

	//ANALOG OUT (PWM)
	for (int i = 0; i < MAX_ANALOG_OUT; i++)
	{
	    if (pinNotPresent(ignored_int_outputs, ARRAY_SIZE(ignored_int_outputs), i))
    		if (int_output[i] != NULL) pwmWrite(analogOutBufferPinMask[i], (int_output[i] / 64));
	}

	pthread_mutex_unlock(&bufferLock); //unlock mutex
}

// 函数来计算非零元素的数量
int countNonZeroElements(int arr[MAX_ROWS][MAX_COLS], int rows, int cols) {
    int count = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (arr[i][j] != 0) {
                count++;
            }
        }
    }
    return count;
}
// 计算非零元素数量的函数
int countNonZeroElementsUint8(uint8_t arr[][MAX_COLS], int rows, int cols) {
    int count = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (arr[i][j] != 0) {
                count++;
            }
        }
    }
    return count;
}
char * handleReadAction(){
	printf("树莓派远端向传输数据");
    char  *json_str;
    // 创建JSON对象
    cJSON *root = cJSON_CreateObject();
    cJSON *matrix = cJSON_CreateArray();
    //Read raspberrypi GPIO
    updateBuffersIn();
     
	int nonZeroCount = countNonZeroElements(array, MAX_ROWS, MAX_COLS);

    // 第一行存储原数组的行数、列数和非零元素的数量
    int sparse[nonZeroCount + 1][3];
    sparse[0][0] = MAX_ROWS;
    sparse[0][1] = MAX_COLS;
    sparse[0][2] = nonZeroCount;

    // 转换二维数组到稀疏数组形式
    int k = 1; // 稀疏数组的初始下标
    for (int i = 0; i < MAX_ROWS; i++) {
        for (int j = 0; j < MAX_COLS; j++) {
            if (array[i][j] != 0) {
                sparse[k][0] = i;
                sparse[k][1] = j;
                sparse[k][2] = array[i][j];
                k++;
            }
        }
    }
    cJSON_AddItemToObject(root, "bool_input", matrix);
    for (int i = 0; i < nonZeroCount + 1; i++) {
        cJSON *row = cJSON_CreateIntArray(sparse[i], 3);
        cJSON_AddItemToArray(matrix, row);
    }
    json_str = cJSON_Print(root);
	printf("%s",json_str);
    // freeBoolInput();
    return json_str;
}
void *tcp_server_callBackFun(void *ptr)
{
	//int new_sockfd = (int *)ptr;//错误，不能直接使用地址，防止外部地址数值改变
	int new_sockfd = *(int *)ptr;
	
	printf("开启线程服务处理客户端(new_sockfd=%d)\n", new_sockfd);
	
	char recv_buff[1024*10] = {0};
	int recv_len = 0;
	
	char *str = NULL;
	
	while (1) {
		memset(recv_buff, 0, sizeof(recv_buff));
		
		recv_len = tcp_blocking_rcv(new_sockfd, recv_buff, sizeof(recv_buff));//堵塞接收消息
		if(0 > recv_len) {
			printf("接收客户端消息失败(new_sockfd=%d)!\n", new_sockfd);
			break;
		} else if(0 == recv_len) {
			printf("客户端断开连接(new_sockfd=%d)\n", new_sockfd);
			break;
		} else {
			printf("收到客户端消息(new_sockfd=%d):%s\n", new_sockfd, recv_buff);
			str = (char *)"服务端已收到";
			char *json_str;
			//解析JSON字符串
			cJSON *root = cJSON_Parse(recv_buff);
            if (root == NULL) {
                // 处理解析错误
                printf("parse error");
                // close(newsockfd);
                // return -1;
                continue;
            }

            printf("获取model字段");
            cJSON* model = cJSON_GetObjectItem(root, "run_model"); 
            if (!cJSON_IsString(model) || (model->valuestring == NULL)) {
                // 处理缺失或错误的"model"字段
                cJSON_Delete(root);
				continue;
            }

            // 比较"model"字段并调用相应的函数
            int result;
            if (strcmp(model->valuestring, "R") == 0) {
				printf("R");
				json_str=handleReadAction();
				printf("%s",json_str);
            } else if (strcmp(model->valuestring, "W") == 0) {
				printf("W");
				handleWriteAction(root);
                json_str="{\"result\":\"OK\"}";
            } else {
                // 处理未知的"model"值
                result = -1;
            }            
			tcp_send(new_sockfd, json_str, strlen(json_str));
		}
		// usleep(1*1000);
	}
	
	tcp_close(new_sockfd);
	
	printf("退出线程服务(new_sockfd=%d)\n", new_sockfd);
	return NULL;
}



int main(int argc, char *argv[])
{
	int ret = 0;

	int sockfd = -1;
	sockfd = tcp_creat_socket();//创建socket
	if (0 > sockfd) {
		printf("socket创建失败...!\n");
		return -1;
	}
	
	int port = 2022;
	char *ip = (char *)"127.0.0.1";
	
	ret = tcp_server_bind_and_listen(sockfd, ip, port, 1024);
	if (0 > ret) {
		printf("bind_and_listen失败...!\n");
		tcp_close(sockfd);
		return -1;
	}
	printf("服务端ip=localHost, 端口=%d\n", port);
	
	int new_sockfd = -1;
	while (1) {
		if (0 > (new_sockfd = tcp_server_wait_connect(sockfd))) {//堵塞直到客户端连接
			printf("等待连接失败...!\n");
			continue;
		} else {
			printf("\n有客户端连接成功! new_sockfd=%d\n", new_sockfd);
			tcp_server_creat_pthread_process_client(&new_sockfd, tcp_server_callBackFun);//服务端每接收到新的客户端连接,就创建新线程提供服务
		}
	}
	
	tcp_close(sockfd);

	return 0;
}
