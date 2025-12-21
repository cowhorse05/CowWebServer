#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <webserver/cow_http_connection.hpp>
//定义http相应状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You don't have permission to get file from this server";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem sering the requested file";

const char* doc_root = "/home/liyufeng/cpp_projects/webserver/resources";

// 静态成员初始化，否则未定义行为
int CowHttpConnection::m_epollfd = -1;
int CowHttpConnection::m_user_cnt = 0;

void setnonblocking(int fd) {
    int old = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old | O_NONBLOCK);
}
void print_events(uint32_t events) {
    if (events & EPOLLIN)
        printf(" EPOLLIN");
    if (events & EPOLLOUT)
        printf(" EPOLLOUT");
    if (events & EPOLLERR)
        printf(" EPOLLERR");
    if (events & EPOLLHUP)
        printf(" EPOLLHUP");
    if (events & EPOLLRDHUP)
        printf(" EPOLLRDHUP");
    if (events & EPOLLONESHOT)
        printf(" EPOLLONESHOT");
}
//向epoll添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {

    struct epoll_event event;
    // event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    event.data.fd = fd;
    if (one_shot) {
        event.events = event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //非阻塞
    setnonblocking(fd);
    printf("[addfd] epollfd=%d add fd=%d oneshot=%d\n", epollfd, fd, one_shot);
}
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modifyfd(int epollfd, int fd, int eve) {
    struct epoll_event event;
    event.events = eve | EPOLLONESHOT | EPOLLRDHUP;
    event.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    printf("[modifyfd] fd=%d new_events=", fd);
    print_events(eve | EPOLLONESHOT | EPOLLRDHUP);
    printf("\n");
}
void CowHttpConnection::init(int sockfd, const sockaddr_in& fd_addr) {
    this->m_sockfd = sockfd;
    this->m_address = fd_addr;
    //添加到epoll
    addfd(m_epollfd, this->m_sockfd, true);
    m_user_cnt++; //线程不安全
    process_read_arg_init();
}

void CowHttpConnection::close_connection() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1; //防止重复close
        m_user_cnt--;  //线程不安全
    }
    printf("[close] fd=%d\n", m_sockfd);
}
bool CowHttpConnection::read() { //一次性读完
    // printf("read data from user\n");
    if (m_read_idx > READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while (1) { //直到无数据，或者用户关闭连接
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return -1;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("read data:\n%s", m_read_buf);
    return true;
}

bool CowHttpConnection::write() {
    printf("send data to user\n");
    return true;
}
void CowHttpConnection::process_read_arg_init() {
    state_ = State::READING;
    m_check_state = CheckState::CHECK_STATE_REQUESTLINE; //请求解析首行
    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;

    //初始化请求信息
    m_method = RequestMethod::GET;
    m_url = nullptr;

    bzero(m_read_buf, READ_BUFFER_SIZE);
}
void CowHttpConnection::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}
//得到一个正确的Http请求，分析目标文件的属性
HttpCode CowHttpConnection::do_request() {
    //前面已经进行了合法性检查，此处直接拼接
    char real_path[512];
    bzero(real_path, sizeof(real_path));
    int len = strlen(doc_root);
    strncpy(real_path, doc_root,
            sizeof(real_path) - 1); // strncpy是最多复制的字节数
    strncat(real_path, m_url, sizeof(real_path) - strlen(real_path) - 1);

    //没有参数则直接返回index.html
    if (m_url[strlen(m_url) - 1] == '/') {
        strncat(real_path, "index.html",
                sizeof(real_path) - strlen(real_path) - 1);
    }
    if (stat(real_path, &m_file_stat) < 0) {
        return HttpCode::NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) { //访问权限
        return HttpCode::FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) { //是否目录
        return HttpCode::BAD_REQUEST;
    }
    m_real_file = real_path;
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_address = (char*)mmap(nullptr, m_file_stat.st_size, PROT_READ,
                                 MAP_PRIVATE, fd, 0);
    if (m_file_address == MAP_FAILED) {
        close(fd);
        return HttpCode::INTERNAL_ERROR;
    }
    close(fd);
    return HttpCode::FILE_REQUEST;
}

CowHttpConnection::LineStatus CowHttpConnection::parse_line() {
    char tmp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        tmp = m_read_buf[m_checked_idx];
        if (tmp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LineStatus::LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LineStatus::LINE_OK;
            }
            return LineStatus::LINE_BAD;
        } else if (tmp == '\n') {
            if ((m_checked_idx > 1) &&
                (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LineStatus::LINE_OK;
            }
            return LineStatus::LINE_BAD;
        }
    }
    return LineStatus::LINE_OPEN;
}
RequestMethod parse_method(char* method) {
    if (strcasecmp(method, "GET") == 0)
        return RequestMethod::GET;
    if (strcasecmp(method, "POST") == 0)
        return RequestMethod::POST;
    if (strcasecmp(method, "HEAD") == 0)
        return RequestMethod::HEAD;
    if (strcasecmp(method, "PUT") == 0)
        return RequestMethod::PUT;
    if (strcasecmp(method, "DELETE") == 0)
        return RequestMethod::DELETE_;
    if (strcasecmp(method, "OPTIONS") == 0)
        return RequestMethod::OPTIONS;
    if (strcasecmp(method, "TRACE") == 0)
        return RequestMethod::TRACE;
    if (strcasecmp(method, "CONNECT") == 0)
        return RequestMethod::CONNECT;
    return RequestMethod::UNKNOWN;
}
//解析请求首行
HttpCode CowHttpConnection::parse_request_line(char* text) {
    // GET / HTTP/1.1      // GET /index.html /HTTP/1.1
    //不采用正则表达式<method> <url> <version>太慢
    //<method> SP <url> SP <version> CRLF
    // GET /index.html HTTP/1.1\0\0 解析之后是这样
    // method
    char* url = strpbrk(text, " \t"); // space || tab，所以判断两个
    if (!url) {
        return HttpCode::BAD_REQUEST;
    }
    *url++ = '\0'; //截断
    /// index.html\t /HTTP/1.1\0\0

    m_method = parse_method(text); // GET\0
    if (m_method == RequestMethod::UNKNOWN) {
        return HttpCode::BAD_REQUEST;
    }

    url += strspn(url, " \t"); //此时*url= ' '?

    // version
    char* version;
    version = strpbrk(url, " \t");
    if (!version) {
        return HttpCode::BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");

    if (strcasecmp(version, "HTTP/1.1") != 0) {
        return HttpCode::BAD_REQUEST;
    }
    // update member
    m_version = version;
    m_url = url;

    // deal url
    if (strncasecmp(m_url, "http://", 7) == 0) {
        //如果是http://192.168.1.1：10000/index.html
        m_url += 7;
        m_url = strchr(m_url, '/');
        if (!m_url) {
            return HttpCode::BAD_REQUEST;
        }
    } else if (!m_url || m_url[0] != '/') { //    /index.html
        return HttpCode::BAD_REQUEST;
    }

    m_check_state = CheckState::CHECK_STATE_HEADER;
    return HttpCode::NO_REQUEST;
}
//解析请求头
HttpCode CowHttpConnection::parse_headers(char* text) {
    if (text[0] == '\0') {
        //如果有请求体，m_content_length
        if (m_content_length != 0) {
            m_check_state = CheckState::CHECK_STATE_CONTENT;
            return HttpCode::NO_REQUEST;
        }
        return HttpCode::GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else if (strncasecmp(text, "Content-Type:", 13) == 0) {
        //这个是Post请求的情况，不会处理
        text += 13;
        text += strspn(text, " \t");
        m_content_type = text;
    } else {
        printf("oop! unknow header %s\n", text);
    }
    return HttpCode::NO_REQUEST;
}

HttpCode CowHttpConnection::parse_contents() {
    if (m_read_idx >= m_checked_idx + m_content_length) {
        // content 已完整读入
        m_read_buf[m_checked_idx + m_content_length] = '\0'; // 可选
        m_content = m_read_buf + m_checked_idx;
        return HttpCode::GET_REQUEST;
    }

    // content 还没读完整
    return HttpCode::NO_REQUEST;
}
// process里面的子状态的主状态机
//数据驱动 FSM
HttpCode CowHttpConnection::process_read() {
    LineStatus line_status = LineStatus::LINE_OK;
    HttpCode ret = HttpCode::NO_REQUEST;
    char* text = nullptr;
    while (true) {
        if (m_check_state != CheckState::CHECK_STATE_CONTENT) {
            line_status = parse_line();
            if (line_status == LineStatus::LINE_BAD)
                return HttpCode::BAD_REQUEST;
            if (line_status == LineStatus::LINE_OPEN)
                break;
            text = get_line();
            m_start_line = m_checked_idx;
            //非content的情况下才更新，因为content可能没有换行符
        }

        // printf("get one http line %s", text);
        switch (m_check_state) {
        case CheckState::CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == HttpCode::BAD_REQUEST) {
                return ret;
            }
            break;

        case CheckState::CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if (ret == HttpCode::BAD_REQUEST) {
                return ret;
            } else if (ret == HttpCode::GET_REQUEST) {
                return do_request();
            }
            break;

        case CheckState::
            CHECK_STATE_CONTENT: //此处不是按行解析，本项目对此不作解析
            ret = parse_contents();
            if (ret == HttpCode::BAD_REQUEST) {
                return ret;
            } else if (ret == HttpCode::GET_REQUEST) {
                return do_request();
            }
            return HttpCode::NO_REQUEST;
        default:
            ret = HttpCode::INTERNAL_ERROR;
            break;
        }
    }
    return ret;
}
HttpCode CowHttpConnection::process_write() {}
HttpCode CowHttpConnection::process_process() {}

//线程池工作调用，连接调度的FSM
void CowHttpConnection::process() {
    //解析
    printf("parse http request,create response\n");

    //有限状态机
    switch (state_) {
    case State::READING: {
        HttpCode read_ret = process_read();
        if (read_ret == HttpCode::NO_REQUEST) {
            modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        }else{
            state_ = State::PROCESSING;
        }
        break;
    }
    case State::WRITING: {
        if(!write()){
            modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
        }else{
            state_ = m_linger ? State::READING : State::CLOSED;
        }
        break;
    }
    case State::PROCESSING: {
        process_write(); //构造响应
        modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
        state_ = State::WRITING;
        break;
    }
    case State::CLOSED: {
        close_connection();
    }
    default:
        break;
    }
    //生成响应
}