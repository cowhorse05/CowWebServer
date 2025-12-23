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
#include <memory>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

#define MAX_FD 65535
#define EPOLL_SIZE 50
#define MAX_EVENT_NUMBER 10000

//处理错误
void error_handling(std::string message) {
    printf("%s error", message.c_str());
    exit(-1);
}
//添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//增删改epoll 的 文件描述符
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void modifyfd(int epollfd, int fd, int event);
extern void close_connection();
extern void print_events(uint32_t events);

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
    CowThreadPool<CowHttpConnection>* pool = nullptr;
    // std::unique_ptr<CowThreadPool<CowHttpConnection>> pool;
    try {
        pool = new CowThreadPool<CowHttpConnection>;
    } catch (...) {
        throw std::exception();
        exit(-1);
    }

    //创建数组用于保存客户端信息
    CowHttpConnection* user_con = new CowHttpConnection[MAX_FD];

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
    if (bind(serverfd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        error_handling("bind()");
    }
    if (listen(serverfd, 5) != 0) {
        error_handling("listen()");
    }
    //创建epoll，事件数组
    epoll_event ep_events[MAX_EVENT_NUMBER];

    struct epoll_event event;
    int epollfd, event_cnt;
    epollfd = epoll_create(EPOLL_SIZE);
    CowHttpConnection::m_epollfd = epollfd;
    event.data.fd = serverfd;
    event.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, serverfd, &event);

    while (1) {
        event_cnt = epoll_wait(epollfd, ep_events, MAX_EVENT_NUMBER, -1);
        if (event_cnt < 0) {
            if (errno == EINTR) {
                // 被信号中断，继续等
                continue;
            }
            perror("epoll_wait");
            break;
        }
        //循环处理连接
        for (int i = 0; i < event_cnt; ++i) {
            int sockfd = ep_events[i].data.fd;
            uint32_t ev = ep_events[i].events;
            printf("[event %d] fd=%d events=", i, sockfd);
            print_events(ev);
            printf("\n");
            // reactor
            if (sockfd == serverfd) { //连接成功
                if (sockfd == serverfd) {
                    printf(">> accept event on serverfd\n");

                    sockaddr_in request_addr;
                    socklen_t req_addr_size = sizeof(request_addr);
                    int confd =
                        accept(serverfd, (struct sockaddr*)&request_addr,
                               &req_addr_size);
                    printf("Accepted connection from %s:%d\n",
                           inet_ntoa(request_addr.sin_addr),
                           ntohs(request_addr.sin_port));
                    if (CowHttpConnection::m_user_cnt >= MAX_FD) {
                        //给客户的报错可以整理成一个error的enum，到时候直接传入，这里不作处理；
                        std::string err =
                            "server is busy,please wait for some time";
                        write(confd, err.c_str(), err.size());
                        close(confd);
                        continue;
                    }
                    //初始化客户数据，利用连接的描述符作为索引
                    user_con[confd].init(confd, request_addr);
                }
            } else if (ev & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)) {
                //错误断开了
                user_con[sockfd].close_connection();
            } else if (ev & EPOLLIN) {
                if (user_con[sockfd].read()) {
                    pool->append(&user_con[sockfd]); //加入线程池
                    // pool->append(user_con + sockfd);
                } else {
                    user_con[sockfd].close_connection();
                }
            } else if (ev & EPOLLOUT) {
                if (!user_con[sockfd].write()) {
                    user_con[sockfd].close_connection();
                }
            }
        }
    }
    close(serverfd);
    close(epollfd);

    delete[] user_con;
    delete pool;
    return 0;
}