/*************************************************
Function:tcp 客户端进程，服务器中可运行多个
author:zyh
date:2020.4
**************************************************/
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<pthread.h>
#include"tcp_spi.h"
#include "cJSON.h"

int sockfd = -1;//客户端socket通信句柄
int tcp_client_connectflag = 0;//客户端socket通信连接标志
int array[1024][8];
#define MAX_ROWS 1024
#define MAX_COLS 8
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
//开启一个接收消息的线程
void *fun_client_rcv(void *ptr)
{
	char recvBuf[1024*20] = {0};
	int recvBytes = 0;
	
	while (1) {
		if (1 == tcp_client_connectflag) {
			//堵塞接收
			memset(recvBuf, 0, sizeof(recvBuf));//清空
			recvBytes = tcp_blocking_rcv(sockfd, recvBuf, sizeof(recvBuf));//堵塞接收
			if (0 > recvBytes) {//接收失败
				printf("接收失败\n");
				tcp_client_connectflag = 0;
			} else if (0 == recvBytes) {//断开了连接
				printf("已断开连接\n");
				tcp_client_connectflag = 0;
			} else {
				// printf("接收到消息:%s\n", recvBuf);
				printf("解析JSON\n");
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
				// toOriginalArray(sparse,array);
				// 清理JSON对象
				cJSON_Delete(root);				
			}
		} else {
			sleep(1);
		}
	}
	
	return NULL;
}

//开启一个发送消息的线程
void *fun_client_send(void *ptr)
{
	char msg_buf[1024] = {0};
	
	while (1) {
		if (1 == tcp_client_connectflag) {//连接成功
			printf("\n请输入要发送的消息:\n");
			scanf("%s", msg_buf);
			printf("正在发送\n");
			if (0 > tcp_send(sockfd, msg_buf, strlen(msg_buf))) {//如果含有0x00不能用strlen
				printf("发送失败...！\n");
				tcp_client_connectflag = 0;
			} else {
				printf("发送成功\n");
			}
			sleep(1);
		} else {
			sleep(1);
		}
	}
	
	return NULL;
}


int main(int argc, char *argv[])
{
	char server_ip[16] = {0};//服务器IP
	int server_port = 0;//服务器端口
	int ret = 0;
	
	pthread_t thread_client_rcv, thread_client_send;

	//创建一个接收消息线程
	ret = pthread_create(&thread_client_rcv, NULL, fun_client_rcv, NULL);
	if (ret < 0) {
		printf("creat thread_client_rcv is fail!\n");
		return -1;
	}
	
	ret = pthread_create(&thread_client_send, NULL, fun_client_send, NULL);
	if (ret < 0) {
		printf("creat fun_client_send is fail!\n");
		return -1;
	}
		
	printf("请输入服务器ip:\n");
	scanf("%s", server_ip);
	
	printf("请输入服务器端口:\n");
	scanf("%d", &server_port);

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
	pthread_join(thread_client_rcv, NULL);
	pthread_join(thread_client_send, NULL);
	return 0;
}
