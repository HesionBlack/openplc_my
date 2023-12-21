//-----------------------------------------------------------------------------
// Copyright 2015 Thiago Alves
//
// Based on the LDmicro software by Jonathan Westhues
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This file is the hardware layer for the OpenPLC. If you change the platform
// where it is running, you may only need to change this file. All the I/O
// related stuff is here. Basically it provides functions to read and write
// to the OpenPLC internal buffers in order to update I/O state.
// Thiago Alves, Dec 2015
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "ladder.h"
#include "custom_layer.h"
#include "../cJSON.h"
#include "../tcp_spi.h"
#define SERVER_ADDR "192.168.43.215"
#define SERVER_PORT "1502"
#define MAX_ROWS 1024
#define MAX_COLS 8
unsigned char log_msg[1000];
int sockfd = -1;//客户端socket通信句柄
int tcp_client_connectflag = 0;//客户端socket通信连接标志
// 还原稀疏数组为原始的二维数组
void toOriginalArray(int sparse[][3], int original[][MAX_COLS]) {
    // 首先初始化原始数组为零
    for (int i = 0; i < sparse[0][0]; i++) {
        for (int j = 0; j < sparse[0][1]; j++) {
            original[i][j] = 0;
        }
    }
    
    // 遍历稀疏数组来填充原始数组
    for (int i = 1; i <= sparse[0][2]; i++) {
        int row = sparse[i][0];
        int col = sparse[i][1];
        int value = sparse[i][2];
        
        original[row][col] = value;
    }
}
// 计算非零元素数量的函数
int countNonZeroElements(IEC_BOOL* arr[][MAX_COLS], int rows, int cols) {
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
// 二维数组转稀疏数组的函数
void toSparseArray(IEC_BOOL* original[][MAX_COLS], int rows, int cols, int sparse[][3]) {
    int k = 1;  // 稀疏数组的行计数器，从1开始因为0行用于存储元数据
    sparse[0][0] = rows;
    sparse[0][1] = cols;
    sparse[0][2] = countNonZeroElements(original, rows, cols);

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (original[i][j] != 0) {
                sparse[k][0] = i;
                sparse[k][1] = j;
                sparse[k][2] = original[i][j];
                k++;
            }
        }
    }
}
void *wirteDataToPi(void *ptr){
	pthread_mutex_lock(&bufferLock); //lock mutex
	char* json_str;

	if(tcp_client_connectflag==0){
		return -1;
	}
	//往树莓派写数据
	sprintf(log_msg, "往树莓派写数据...\n");
	log(log_msg);	
	int ret = 0;
	char recvBuf[1024*8] = {0};
	int recvBytes = 0;
	cJSON *root = cJSON_CreateObject();
    cJSON *matrix = cJSON_CreateArray();
	cJSON_AddStringToObject(root,"run_model","W");
	cJSON_AddNumberToObject(root,"int_output",*int_output[0]);
	cJSON_AddItemToObject(root, "bool_input", matrix);
	//bool_output 转成稀疏数组
	// 计算非零元素数量
    int nonZeroCount = countNonZeroElements(bool_output,1024,8);	
	// 创建稀疏数组，行数为非零元素数量加1，列数固定为3
    int sparse[nonZeroCount + 1][3];
    // 填充稀疏数组
    toSparseArray(bool_output, 1024, 8, sparse);	
    for (int i = 0; i < nonZeroCount + 1; i++) {
        cJSON *row = cJSON_CreateIntArray(sparse[i], 3);
        cJSON_AddItemToArray(matrix, row);
    }
    json_str = cJSON_Print(root);
	printf("正在发送\n");
	if (0 > tcp_send(sockfd, json_str, strlen(json_str))) {//如果含有0x00不能用strlen
		printf("发送失败...！\n");
		tcp_client_connectflag = 0;
	} else {
		printf("发送成功\n");
	}
	//堵塞接收
	memset(recvBuf, 0, sizeof(recvBuf));//清空
	recvBytes = tcp_blocking_rcv(sockfd, recvBuf, sizeof(recvBuf));//堵塞接收
	if (0 > recvBytes) {//接收失败
		sprintf(log_msg, "接收失败\n");
		log(log_msg);	
	} else if (0 == recvBytes) {//断开了连接
		sprintf(log_msg, "已断开连接\n");
		log(log_msg);	
	}else{
		sprintf(log_msg, "收到信息:%s\n",recvBuf);
		log(log_msg);	
	}
	pthread_mutex_unlock(&bufferLock); //unlock mutex
}

void *readPiData(void *ptr){
	//从树莓派远程获取针脚数据

	if(tcp_client_connectflag==0){
		sprintf(log_msg, "未连接服务器...!\n");
		log(log_msg);	
		return -1;
	}
	sprintf(log_msg, "从树莓派远程获取针脚数据...!\n");
	log(log_msg);		
	pthread_mutex_lock(&bufferLock); //lock mutex
	int ret = 0;
	char recvBuf[1024*8] = {0};
	int recvBytes = 0;
	char* json_str;
	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root,"run_model","R");
	json_str= cJSON_Print(root);
	printf("正在发送\n");
	if (0 > tcp_send(sockfd, json_str, strlen(json_str))) {//如果含有0x00不能用strlen
		printf("发送失败...！\n");
		tcp_client_connectflag = 0;
	} else {
		printf("发送成功\n");
	}
	//堵塞接收
	memset(recvBuf, 0, sizeof(recvBuf));//清空
	recvBytes = tcp_blocking_rcv(sockfd, recvBuf, sizeof(recvBuf));//堵塞接收
	if (0 > recvBytes) {//接收失败
		sprintf(log_msg, "接收失败\n");
		log(log_msg);	
	} else if (0 == recvBytes) {//断开了连接
		sprintf(log_msg, "已断开连接\n");
		log(log_msg);	
	} else {
		// printf("接收到消息:%s\n", recvBuf);
		sprintf(log_msg, "解析JSON\n");
		log(log_msg);	
		cJSON *root = cJSON_Parse(recvBuf);
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
			*bool_input[x][y]=(uint8_t)sparse[2];
		}
		// 清理JSON对象
		cJSON_Delete(root);	
		tcp_close(sockfd);			
	}
	pthread_mutex_unlock(&bufferLock); //unlock mutex
}
void *fun_client_connect(void *ptr){
	while (1) {
		if (0 == tcp_client_connectflag) {//未连接就不断中断重连
	
			if (sockfd > 0) {  //sockfd等于0时不能关，防止把文件句柄0关掉，导致scanf()函数输入不了
				tcp_close(sockfd);
			}
			
			sockfd = tcp_creat_socket();//创建socket
			if (0 > sockfd) {
				printf("socket创建失败...!\n");
				sleep(2);
				continue;
			}
			
			printf("请求连接...\n");
			if (0 > tcp_client_connect(sockfd, server_ip, server_port)) {
				printf("连接失败...重连中...\n");
				sleep(2);
				continue;
			} else {
				tcp_client_connectflag = 1;
				printf("连接成功!\n");
			}	
		} else {
			sleep(1);
		}
	}
	
	tcp_close(sockfd);	
}
//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	int ret = 0;
	pthread_t thread_client_connect_holder;		
	//创建一个维持连接的线程
	ret = pthread_create(&thread_client_connect_holder, NULL, fun_client_connect, NULL);
	if (ret < 0) {
		printf("creat thread_client_rcv is fail!\n");
		return -1;
	}	
}

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is finalizing.
// Resource clearing procedures should be here.
//-----------------------------------------------------------------------------
void finalizeHardware()
{
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual Input state. The mutex bufferLock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersIn()
{
	pthread_t thread_client_read_pi;
	int ret = 0;
	ret = pthread_create(&thread_client_read_pi, NULL, readPiData, NULL);
	if (ret < 0) {
		printf("creat thread_client_rcv is fail!\n");
		return -1;
	}
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual Output state. The mutex bufferLock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffersOut()
{
	pthread_t thread_client_write_data_to_pi;
	int ret = 0;
	ret = pthread_create(&thread_client_write_data_to_pi, NULL, wirteDataToPi, NULL);
	if (ret < 0) {
		printf("creat thread_client_rcv is fail!\n");
		return -1;
	}
}

