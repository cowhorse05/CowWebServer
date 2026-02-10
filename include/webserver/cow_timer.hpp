#pragma once

#include <queue>
#include <time.h>
#include <vector>
//前向声明
class CowHttpConnection;

class TimerNode {
  public:
    time_t expire;
    CowHttpConnection* conn;
    bool deleted;

    TimerNode(CowHttpConnection* c, int timeout)
        : conn(c)
        , deleted(false) {
        expire = time(nullptr) + timeout;
    }
};
//比较器
struct TimerCmp {
    bool operator()(TimerNode* u, TimerNode* v) {
        return u->expire > v->expire;
    }
};

class TimerManager {
  public:
    TimerManager() = default;
    ~TimerManager();
    TimerManager(const TimerManager&) = delete;
    
    TimerManager& operator=(const TimerManager&) = delete;
    void add_timer(CowHttpConnection* conn, int timeout);
    void adjust_timer(TimerNode* timer, int timeout);
    void handle_expired();

  private:
    std::priority_queue<TimerNode*, std::vector<TimerNode*>, TimerCmp>
        m_timer_heap;
};
