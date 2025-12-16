#pragma once

#include <time.h>
#include<pthread.h>
#include<exception>
#include<semaphore.h>
#include<assert.h>

/*--------互斥锁类------------*/
class CowLocker{
public:
    CowLocker(){
        if(pthread_mutex_init(&mutex_, NULL)){
            throw std::exception();
        } 
    }
    ~CowLocker(){
        pthread_mutex_destroy(&mutex_);
    }
    CowLocker(const CowLocker&) = delete;
    CowLocker& operator=(const CowLocker&) = delete;
    
    bool lock(){
        return pthread_mutex_lock(&mutex_) == 0;
    }
    
    bool unlock(){
        return pthread_mutex_unlock(&mutex_) == 0;
    }
    pthread_mutex_t* native_handle(){
        return &mutex_;
    }

    
private:
    pthread_mutex_t mutex_;

};

/*---------------条件量-----------*/
class CowCondition{
public:
    CowCondition(){
        pthread_cond_init(&m_condition, NULL);
    }
    ~ CowCondition(){
        int ret = pthread_cond_destroy(&m_condition);
        assert(ret == 0);
    }
    CowCondition(const CowCondition&) = delete;
    CowCondition& operator=(const CowCondition&) = delete;
    bool wait(pthread_mutex_t* mutex){
        return pthread_cond_wait(&m_condition, mutex) == 0;
    }
    bool timewait(pthread_mutex_t* mutex, const struct timespec time){
        return pthread_cond_timedwait(&m_condition, mutex,&time) == 0;
    }

    bool signal(){
        return pthread_cond_signal(&m_condition) == 0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&m_condition) == 0;
    }

private:
    pthread_cond_t m_condition;

};

/*-------------信号量------------*/
class CowSemaphore{
public:
    CowSemaphore(){
        if(sem_init(&m_semaphore, 0, 0) != 0){
            throw std::exception();
        }
    }
    CowSemaphore(int value){
        sem_init(&m_semaphore, 0, value);
    }
    ~CowSemaphore(){
        sem_destroy(&m_semaphore);
    }
    CowSemaphore(const CowSemaphore&) = delete;
    CowSemaphore& operator=(const CowSemaphore&) = delete;
    bool wait(){
        return sem_wait(&m_semaphore) == 0 ;
    }

    bool post(){
        return sem_post(&m_semaphore) == 0;
    }
private:
    sem_t m_semaphore;
};

