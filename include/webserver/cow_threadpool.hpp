#pragma once
#include "cow_locker.hpp"
#include <exception>
#include <pthread.h>
#include <queue>
#include <semaphore.h>
#include<stdio.h>
//模板类
/*-----------线程池---------------*/
template <typename T> class CowThreadPool {
  public:
   /*thread_number是线程池中线程的数量，
   max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    CowThreadPool(int thread_number = 8, int max_request = 1e5);
    ~CowThreadPool();
    CowThreadPool(const CowThreadPool&) = delete;
    CowThreadPool& operator=(const CowThreadPool&) = delete;
    bool append(T* request);  //生产者

  private:
    static void* worker(void* arg); //消费者//必须普通函数指针，而且静态,成员隐含this
    void run();

  private:
    int m_thread_number;        //线程数目
    pthread_t* m_threads;       //线程池数组
    int m_max_request;          //最大请求数目
    std::queue<T*> m_workqueue; //可以换成list
    CowLocker m_queuelocker;    //互斥锁
    CowSemaphore m_queuestat;   //信号量
    bool m_stop;                //线程结束
};


template <typename T>
CowThreadPool<T>::CowThreadPool(int thread_number, int max_requests)
    : m_thread_number(thread_number)
    , m_max_request(max_requests)
    , m_stop(false)
    , m_threads(nullptr) {

    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[thread_number];
    if (m_threads == nullptr) {
        delete[] m_threads;
        throw std::exception();
    }

    //创建thread_number个线程，并将他们设置为脱离线程
    for (int i = 0; i < thread_number; ++i) {
        printf("create the %d thread\n", i);
        if (pthread_create(&m_threads[i], NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        //脱离线程：线程结束后，系统自动回收其资源
        //避免僵尸进程
        if (pthread_detach(m_threads[i]) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
CowThreadPool<T>::~CowThreadPool(){
    delete[] m_threads;
    m_stop = true;
}


/*经典线程池队列实现，互斥锁和信号量分工明确
生产者消费者模型，
互斥锁保护任务队列的线程安全
信号量控制工作线程的阻塞/唤醒
*/
template <typename T>
bool CowThreadPool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_request){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push(request);
    m_queuelocker.unlock(); 
    //通知工作线程，有新任务
    m_queuestat.post();
    return true;
}


template <typename T>
void* CowThreadPool<T>::worker(void* arg){
    CowThreadPool* pool = static_cast<CowThreadPool*>(arg);
    pool->run();
    return nullptr;
}

template <typename T>
void CowThreadPool<T>::run(){
    while(!m_stop){//消费者等任务
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
          m_queuelocker.unlock();
          continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop();
        m_queuelocker.unlock();

        if(!request){//没获取
            continue;
        }
        request->process();
    }
}