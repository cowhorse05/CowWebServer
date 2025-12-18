#pragma once
#include<sys/epoll.h>
class CowHttpConnection {
  public:
    CowHttpConnection() {}
    ~CowHttpConnection() {}
    CowHttpConnection(const CowHttpConnection&) = delete;
    CowHttpConnection& operator=(const CowHttpConnection&) = delete;

    void process(){};

  private:
};