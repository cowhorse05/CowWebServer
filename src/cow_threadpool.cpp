#include "webserver/cow_threadpool.hpp"
#include "webserver/cow_locker.hpp"
#include <cstddef>
#include <exception>
#include <pthread.h>
#include <stdio.h>

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
        printf("create the %d thread", i);
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
    m_workqueue.push_back(request);
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
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){//没获取
            continue;
        }
        request->process();
    }
}