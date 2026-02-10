#pragma once
#include "webserver/cow_timer.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
class TimerNode;
enum class HttpCode {
    NO_REQUEST,        //请求不完整
    GET_REQUEST,       //获得了一个完成的客户请求
    BAD_REQUEST,       //客户端请求语法错误
    INTERNAL_ERROR,    //服务器内部解析错误
    NO_RESOURCE,       //服务器没有资源
    FORBIDDEN_REQUEST, //客户对资源访问无访问权限
    FILE_REQUEST,      //文件请求成功
    CLOSED_CONNECTION  //客户端已经关闭连接
};

enum class RequestMethod {
    GET,
    POST,
    HEAD,
    PUT,
    DELETE_, //这里DELETE是cpp关键字，所以必须区分
    TRACE,
    OPTIONS,
    CONNECT,
    UNKNOWN //永远不要假设输入合法
};
/*--------任务对象类------------*/
class CowHttpConnection {
  public:
    static int
        m_epollfd; //所有socket上的时间都被注册到同一个epoll内核事件中，所以设置成静态
    static int m_user_cnt; //统计用户数量
  public:
    static const int FILENAME_LEN = 200; //文件最大长度
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    CowHttpConnection() {}
    ~CowHttpConnection() {}
    CowHttpConnection(const CowHttpConnection&) = delete;
    CowHttpConnection& operator=(const CowHttpConnection&) = delete;

    void process();
    void init(int sockfd, const sockaddr_in& fd_addr); //初始化新接受的连接
    void close_connection();
    bool read(); //读完数据
    bool write();

  private:
    /*此次本来想写有限状态机，发现自己功底还是太拉了，写不出来*/
    enum class State { READING, PROCESSING, WRITING, CLOSED, FINISHED };
    // read部分
    void process_read_arg_init(); // process_read初始化
    HttpCode process_read();
    HttpCode parse_request_line(char* text); //请求行 行驱动
    HttpCode parse_headers(char* text);      //请求头 行驱动
    HttpCode parse_contents();               //请求体 字节驱动
    HttpCode do_request();

    char* get_line() { return m_read_buf + m_start_line; };
    //写 ,往缓冲区写数据
    bool process_write(HttpCode ret);
    bool add_response(const char* fomat, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);
    bool add_content_type();

  private:
    int m_sockfd;          //通信sock
    sockaddr_in m_address; //通信sock地址

    HttpCode m_read_ret;

    char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区
    int m_read_idx;                    //读取索引
    int m_checked_idx;                 //当前解析的索引
    int m_start_line;                  //当前解析行的起始位置

    // State state_;      // process主状态机
    enum class CheckState {
        CHECK_STATE_REQUESTLINE,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    CheckState m_check_state; // process_read的子状态机

    enum class LineStatus {
        LINE_OK,  //读取到完整的行
        LINE_BAD, //行出错
        LINE_OPEN //行数据不完整
    };
    LineStatus parse_line(); //找/r/n

    RequestMethod parse_method(char* method);
    RequestMethod m_method; //请求方法

    char* m_url;     //请求目标的文件名
    char* m_version; // http 协议版本
    char* m_host;    //主机名
    bool m_linger;   //是否保持连接
    size_t m_content_length;
    char* m_content_type;
    char* m_content;
    char* m_real_file;
    struct stat
        m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address; // 客户请求的目标文件被mmap到内存中的起始位置
    void unmap(); //这里没用上

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    int m_write_sent;
    int m_iv_count; // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量
    struct iovec m_iv[2]; // http响应天然分段 header 和 body
    // m_iv[0] : HTTP response header
    // m_iv[1] : HTTP response body (file)
    ssize_t bytes_to_send;
    ssize_t bytes_have_send;

  public:
    //计时器
    TimerNode* timer;
};