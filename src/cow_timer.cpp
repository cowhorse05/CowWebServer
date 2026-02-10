#include "webserver/cow_timer.hpp"
#include "webserver/cow_http_connection.hpp"




// 析构函数实现
TimerManager::~TimerManager() {
    // 清理所有定时器节点
    while (!m_timer_heap.empty()) {
        TimerNode* node = m_timer_heap.top();
        m_timer_heap.pop();
        delete node;
    }
}
void TimerManager::add_timer(CowHttpConnection* conn, int timeout) {
    TimerNode* timer = new TimerNode(conn, timeout);
    m_timer_heap.push(timer);
    conn->timer = timer;
}

//更新定时器不是修改原timer，是删除timer然后新建一个
//用延迟删除 + 新 timer 的方式更新超时时间，避免破坏最小堆结构
void TimerManager::adjust_timer(TimerNode* timer, int timeout) {
    if (!timer) return;
    timer->deleted = true;
    TimerNode* new_timer = new TimerNode(timer->conn, timeout);
    timer->conn->timer = new_timer;
    m_timer_heap.push(new_timer);
}


void TimerManager::handle_expired(){
    time_t now = time(nullptr);
    
    while(!m_timer_heap.empty()){
        TimerNode* timer = m_timer_heap.top();
        if(timer->deleted){
            m_timer_heap.pop();
            delete timer;
            continue;
        }

        if(timer->expire > now){
            break; //最小堆堆顶都没过期，后面也没过期
        }

        //超时
        timer->conn->close_connection();
        m_timer_heap.pop();
        delete timer;
    }

}