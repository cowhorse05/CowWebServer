#pragma once
#include<sys/epoll.h>
#include <sys/socket.h>
#include<arpa/inet.h>

class CowHttpConnection {
  public:
    static int m_epollfd; 
    static int m_user_cnt;//统计用户数量
    CowHttpConnection() {}
    ~CowHttpConnection() {}
    CowHttpConnection(const CowHttpConnection&) = delete;
    CowHttpConnection& operator=(const CowHttpConnection&) = delete;

    void process();
    void init(int sockfd,const sockaddr_in & fd_addr);
    void close_connection();
    bool read(); //读完数据
    bool write();
  private:
    int m_sockfd; //通信sock
    sockaddr_in m_address; //通信sock地址
};