#pragma once
#include "cow_locker.hpp"
#include <exception>
#include <pthread.h>
#include <queue>
#include <semaphore.h>
#include<stdio.h>

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


template <typename T>
CowThreadPool<T>::CowThreadPool(int thread_number, int max_requests)
    : m_thread_number(thread_number)
    , m_max_request(max_requests)
    , m_stop(false)
    , m_threads(nullptr) {

    if (thread_number < 0 || max_requests < 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[thread_number];
    if (m_threads == nullptr) {
        delete[] m_threads;
        throw std::exception();
    }

    //创建thread_number个线程，并将他们设置为线程脱离
    for (int i = 0; i < thread_number; ++i) {
        printf("create the %d thread\n", i);
        if (pthread_create(&m_threads[i], NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
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