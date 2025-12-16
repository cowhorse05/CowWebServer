#pragma once
#include "cow_locker.hpp"
#include <exception>
#include <pthread.h>
#include <queue>
#include <semaphore.h>

/*-----------线程池---------------*/
template <typename T> class CowThreadPool {
  public:
    CowThreadPool(int thread_number = 8, int max_request = 1e5);
    ~CowThreadPool();
    CowThreadPool(const CowThreadPool&) = delete;
    CowThreadPool& operator=(const CowThreadPool&) = delete;
    bool append(T* request);  //生产者

  private:
    static void* worker(void* arg); //消费者//必须普通函数指针，而且静态,成员隐含this，小伙子记住了
    void run();

  private:
    int m_thread_number;        //线程数目
    pthread_t* m_threads;       //线程池数组
    int m_max_request;          //最大请求数目
    std::queue<T*> m_workqueue; //这里换成list会不会好一点
    CowLocker m_queuelocker;    //互斥锁
    CowSemaphore m_queuestat;   //信号量
    bool m_stop;                //线程结束
};
