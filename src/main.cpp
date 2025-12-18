#include "webserver/cow_http_connection.hpp"
#include "webserver/cow_locker.hpp"
#include "webserver/cow_threadpool.hpp"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <csignal>
#include <cstddef>
#include <errno.h>
#include <exception>
#include <iostream>
#include <libgen.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <threads.h>

#define MAX_FD 65535
#define EPOLL_SIZE 50
#define MAX_EVENT_NUMBER 1e5

//处理错误
void error_handling(std::string message) {
    printf("%s error", message.c_str());
}
//添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//增删epoll 的 文件描述符
extern void addfd(int epollfd,int fd,bool one_shot);
extern void removefd(int epollfd,int fd);


int main(int argc, char* argv[]) {
    int ret = 0;

    if (argc <= 1) {
        printf("usage: %s port_number\n", basename(argv[0]));
        exit(-1);
    }
    //端口
    int port = atoi(argv[1]);
    //对sigpipe信号处理
    addsig(SIGPIPE, SIG_IGN);
    //创建线程池，初始化
    CowThreadPool<CowHttpConnection>* pool = NULL;
    try {
        pool = new CowThreadPool<CowHttpConnection>;
    } catch (...) {
        throw std::exception();
        exit(-1);
    }

    //创建数组用于保存客户端信息
    CowHttpConnection* http_connection = new CowHttpConnection[MAX_FD];

    int serverfd = socket(PF_INET, SOCK_STREAM, 0);
    if (serverfd == -1) {
        error_handling("socket()");
    }
    //端口复用
    int reuse = 1;
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(serverfd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        error_handling("bind()");
    }
    if (listen(serverfd, 5) != 0) {
        error_handling("listen()");
    }
    //创建epoll，事件数组
    struct epoll_event* ep_events;
    struct epoll_event event;
    int epollfd, event_cnt;
    epollfd = epoll_create(EPOLL_SIZE);
    event.data.fd = serverfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, serverfd, &event);

    int requestfd;
    sockaddr_in request_addr;
    while (1) {
        event_cnt = epoll_wait(epollfd, ep_events, EPOLL_SIZE, -1);
        if (event_cnt == -1) {
            puts("epoll wait error");
            break;
        }
        //循环处理连接
        for (int i = 0; i < event_cnt; ++i) {
             else {
            }
        }
    }

    return 0;
}
