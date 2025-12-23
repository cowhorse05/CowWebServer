#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cstdarg>
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
#include <sys/types.h>
#include <unistd.h>
#include <webserver/cow_http_connection.hpp>
//定义http相应状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible\n";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You don't have permission to get file from this server\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem serving the requested file\n";

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
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
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
/*
struct iovec {
    void  *iov_base;  // 缓冲区起始地址（用户空间内存）
    size_t iov_len;   // 缓冲区长度（字节数）
};
writev 返回 n 字节后，要“消费掉”这 n 字节
按顺序优先消费 header，再消费 body
*/
bool CowHttpConnection::write() {
    printf("send data to user\n");
    ssize_t n;
    while (true) {
        if (m_iv_count == 2) {
            n = writev(m_sockfd, m_iv,
                       m_iv_count); //一次性发送多个不连续的数据块
            printf(
                "writev/send returned %ld, m_iv[0].len=%zu, m_iv[1].len=%zu\n",
                n, m_iv[0].iov_len, m_iv[1].iov_len);
        } else {
            n = send(m_sockfd, m_write_buf + m_write_sent,
                     m_write_idx - m_write_sent, 0);
        }
        if (n > 0) {
            bytes_have_send += n;
            printf("Sent %ld bytes, bytes_have_send=%ld\n", n, bytes_have_send);
            if (m_iv_count == 2) {
                // 消费header
                ssize_t remaining = n;
                if (remaining >= (ssize_t)m_iv[0].iov_len) {
                    remaining -= m_iv[0].iov_len; //减去header发送的长度
                    m_iv[0].iov_len = 0;          //发送完 header所以置0
                } else {
                    //只发送了一部分
                    m_iv[0].iov_base = (char*)m_iv[0].iov_base + remaining;
                    m_iv[0].iov_len -= remaining;
                    remaining = 0;
                }
                //消费body
                if (remaining > 0 && m_iv[1].iov_len > 0) {
                    if (remaining >= (ssize_t)m_iv[1].iov_len) {
                        // 整个body都被发送了
                        m_iv[1].iov_len = 0;
                    } else {
                        m_iv[1].iov_base = (char*)m_iv[1].iov_base + remaining;
                        m_iv[1].iov_len -= remaining; //减去剩余发送长度
                    }
                }

                if (m_iv[0].iov_len == 0 && m_iv[1].iov_len == 0) { //写完了
                    return true;
                }
            } else {
                m_write_sent += n;
                if (m_write_sent >= m_write_idx) {
                    return true;
                }
            }
        } else if (n == 0) {
            // 对端关闭连接
            return false;
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满，下次再试
                return false;
            } else {
                // 其他错误
                return false;
            }
        }
    }
}
void CowHttpConnection::process_read_arg_init() {
    bytes_to_send = 0;
    bytes_have_send = 0;
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
void CowHttpConnection::unmap() { //貌似没有使用到
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
    printf("Request file: %s\n", real_path);
    m_real_file = real_path;
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_address = (char*)mmap(nullptr, m_file_stat.st_size, PROT_READ,
                                 MAP_PRIVATE, fd, 0);
    if (m_file_address == MAP_FAILED) {
        close(fd);
        unmap();
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
RequestMethod CowHttpConnection::parse_method(char* method) {
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

    if (strcasecmp(version, "HTTP/1.0") != 0) {
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
        //空行,header结束
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
        printf("[FSM] check_state=%d, line_status=%d\n", m_check_state,
               line_status);
        if (m_check_state != CheckState::CHECK_STATE_CONTENT) {
            line_status = parse_line();
            if (line_status == LineStatus::LINE_BAD)
                return HttpCode::BAD_REQUEST;
            if (line_status == LineStatus::LINE_OPEN)
                return HttpCode::NO_REQUEST;
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

bool CowHttpConnection::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - 1 - m_write_idx) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}
bool CowHttpConnection::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.0", status, title);
}
bool CowHttpConnection::add_headers(int content_len) {
    return add_content_length(content_len) && add_content_type() &&
           add_linger() && add_blank_line();
}
bool CowHttpConnection::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}
bool CowHttpConnection::add_linger() {
    // return add_response("Connection: %s\r\n",
    //                     (m_linger == true) ? "keep-alive" : "close");
    return add_response("Connection: close\r\n");
}
bool CowHttpConnection::add_blank_line() { return add_response("%s", "\r\n"); }
bool CowHttpConnection::add_content(const char* content) {
    return add_response("%s", content);
}
bool CowHttpConnection::add_content_type() {
    return add_response("Content-Type: %s\r\n", "text/html");
}
bool CowHttpConnection::process_write(HttpCode ret) {
    switch (ret) {
    case HttpCode::INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form)) {
            return false;
        }
        break;
    case HttpCode::BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form)) {
            return false;
        }
        break;
    case HttpCode::NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form)) {
            return false;
        }
        break;
    case HttpCode::FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form)) {
            return false;
        }
        break;
    case HttpCode::FILE_REQUEST:
        add_status_line(200, ok_200_title);
        add_headers(m_file_stat.st_size);
        //分散，聚集io
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;
        bytes_to_send = m_write_idx + m_file_stat.st_size;
        return true;
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1; // 默认设置 m_iv_count = 1
    return true;
}

//线程池工作调用，连接调度的FSM
void CowHttpConnection::process() {
    //解析
    printf("parse http request,create response\n");

    // //有限状态机
    // switch (state_) {
    // case State::READING: {
    //     m_read_ret = process_read();
    //     if (m_read_ret == HttpCode::NO_REQUEST) {
    //         modifyfd(m_epollfd, m_sockfd, EPOLLIN);
    //         return; //等待更多数据
    //     }
    //     state_ = State::PROCESSING;
    // }
    // case State::PROCESSING: {
    //     if (process_write(m_read_ret)) {
    //         state_ = State::WRITING;
    //         modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
    //     } else {
    //         close_connection();
    //     }
    //     break;
    // }
    // case State::WRITING: {
    //     if (!write()) {
    //         modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
    //         printf("Need to continue writing...\n");
    //     } else {
    //         // 所有数据发送完成
    //         printf("All data sent successfully\n");
    //         unmap();
    //         if (m_linger) {
    //             //重置连接状态
    //             printf("Keep-alive connection, resetting...\n");
    //             process_read_arg_init();
    //             modifyfd(m_epollfd, m_sockfd, EPOLLIN);
    //             state_ = State::READING;
    //         } else {
    //             // 短连接，等待客户端关闭
    //             printf("Short connection, waiting for client to close...\n");
    //             // 不要立即close_connection()，而是等待EPOLLRDHUP事件
    //             // 可以设置一个标志或者改变状态
    //             state_ = State::FINISHED;
    //             // 监听读事件（等待客户端关闭）
    //             modifyfd(m_epollfd, m_sockfd, EPOLLIN | EPOLLRDHUP);
    //         }
    //     }
    //     break;
    // }
    // case State::FINISHED: {
    //     // 已经发送完响应，等待客户端关闭连接
    //     // 这里什么都不做，等待epoll检测到EPOLLRDHUP或EPOLLIN事件
    //     // 当检测到EPOLLRDHUP时，会调用close_connection()
    //     break;
    // }
    // case State::CLOSED: {
    //     close_connection();
    //     break;
    // }
    // default:
    //     break;
    // }
    // 解析HTTP请求
    HttpCode read_ret = process_read();
    if (read_ret == HttpCode::NO_REQUEST) {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_connection();
    }
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
    if (write()) {
        // 响应已完整发送
        unmap();
        close_connection(); 
    } else {
        modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
    }
}