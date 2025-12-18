#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <webserver/cow_http_connection.hpp>

// 静态成员初始化，否则未定义行为
int CowHttpConnection::m_epollfd = -1;
int CowHttpConnection::m_user_cnt = 0;

void setnonblocking(int fd) {
    int old = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old | O_NONBLOCK);
}
void print_events(uint32_t events) {
    if (events & EPOLLIN)
        printf(" EPOLLIN");
    if (events & EPOLLOUT)
        printf(" EPOLLOUT");
    if (events & EPOLLERR)
        printf(" EPOLLERR");
    if (events & EPOLLHUP)
        printf(" EPOLLHUP");
    if (events & EPOLLRDHUP)
        printf(" EPOLLRDHUP");
    if (events & EPOLLONESHOT)
        printf(" EPOLLONESHOT");
}
//向epoll添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;
    if (one_shot) {
        event.events = event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //非阻塞
    setnonblocking(fd);
    printf("[addfd] epollfd=%d add fd=%d oneshot=%d\n", epollfd, fd, one_shot);
}
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modifyfd(int epollfd, int fd, int eve) {
    struct epoll_event event;
    event.events = eve | EPOLLONESHOT | EPOLLRDHUP;
    event.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    printf("[modifyfd] fd=%d new_events=", fd);
    print_events(eve | EPOLLONESHOT | EPOLLRDHUP);
    printf("\n");
}
void CowHttpConnection::init(int sockfd, const sockaddr_in& fd_addr) {
    this->m_sockfd = sockfd;
    this->m_address = fd_addr;
    //添加到epoll
    addfd(m_epollfd, this->m_sockfd, true);
    m_user_cnt++; //线程不安全
}

void CowHttpConnection::close_connection() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1; //防止重复close
        m_user_cnt--;  //线程不安全
    }
    printf("[close] fd=%d\n", m_sockfd);
}
bool CowHttpConnection::read() { //一次性读完
    printf("read data from user\n");
    return true;
}

bool CowHttpConnection::write() {
    printf("send data to user\n");
    return true;
}

//线程池工作调用
void CowHttpConnection::process() {
    //解析
    printf("parse http request,create response\n");
    //生成响应
}