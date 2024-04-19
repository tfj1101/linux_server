#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096 // 读缓冲区大小
// 主状态机可能状态：当前正在分析请求行，当前正在分析头部字段
enum CHECK_STATE
{
    CHECK_STATE_RUQUESTLINE = 0,
    CHECK_STATE_HEADER
};

// 行的读取状态：1读取到一个完整的行，2行出错，3行数据尚且不完整
enum LINE_STATUS
{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
};

/*  服务器处理HTTP结果：
    NO_REQUEST          请求不完整，需要继续读取客户数据；
    GET_REQUEST         获得了一个完整的客户请求；
    BAD_REQUEST         客户去请求有语法错误
    FORBIDDEN_REQUEST   客户对资源没有足够的访问权限
    INTERNAL_ERROR      服务器内部错误
    CLOSED_CONNECTION   客户端已经关闭连接
*/
enum HTTP_CODE
{
    NO_REQUEST,        // 请求不完整，需要继续读取客户数据；
    GET_REQUEST,       // 获得了一个完整的客户请求；
    BAD_REQUEST,       // 客户去请求有语法错误
    FORBIDDEN_REQUEST, // 客户对资源没有足够的访问权限
    INTERNAL_ERROR,    // 服务器内部错误
    CLOSED_CONNECTION, // 客户端已经关闭连接
};

static const char *szret[] = {"I get a correct result\n", "Something wrong\n"};

// 从状态机，用于解析出一行内容
LINE_STATUS parse_line(char *buffer, int &checked_index, int &read_index)
{
    char temp;
    for (; checked_index < read_index; ++checked_index)
    {
        // 获得当前要分析的字节
        temp = buffer[checked_index];
        // 如果当前的字节使"\r",即回车符，则说明可能读取到一个完整的行
        if (temp == '\r')
        {
            if ((checked_index + 1) == read_index)
            {
                return LINE_OPEN;
            }
            // 如果下一个字符使"\n",则说明我们成功读取到一个完整的行
            else if (buffer[checked_index + 1] == '\n')
            {
                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            // 否则，说明客户发送的HTTP请求存在语法错误
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((checked_index > 1) && buffer[checked_index - 1] == '\r')
            {
                buffer[checked_index - 1] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 分析请求行
HTTP_CODE parse_requestline(char *temp, CHECK_STATE &checkstate)
{
    char *url = strpbrk(temp, "\t");
    if (!url)
    {
        return BAD_REQUEST;
    }
    *url++ = '\0';
    char *method = temp;
    if (strcasecmp(method, "GET") == 0)
    {
        printf("The request method is GET\n");
    }
    else
    {
        return BAD_REQUEST;
    }
    url += strspn(url, "\t");
    char *version = strpbrk(url, "\t");
    if (!version)
    {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, "\t");
    if (strcasecmp(version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    // 检查URL是否合法
    if (strncasecmp(url, "http://", 7) == 0)
    {
        url += 7;
        url = strchr(url, '/');
    }
    if (!url || url[0] != '/')
    {
        return BAD_REQUEST;
    }
    printf("The request URL is: %s", url);
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 分析头部字段
HTTP_CODE parse_headers(char *temp)
{
    // 遇到一个空行，说明我们的得到了一个正确的HTTP请求
    if (temp[0] == '\0')
    {
        return GET_REQUEST;
    }
    else if (strncasecmp(temp, "host:", 5) == 0)
    {
        temp += 5;
        temp += strspn(temp, "\t");
        printf("the request host is:%s\n", temp);
    }
    else
    {
        printf("I can not handle this header\n");
    }
    return NO_REQUEST;
}

// 分析HTTP请求的入口函数
HTTP_CODE parse_content(char *buffer, int &checked_index, CHECK_STATE &checkstate, int &read_index, int &start_line)
{
    LINE_STATUS linestatus = LINE_OK; // 记录当前行的读取状态
    HTTP_CODE retcode = NO_REQUEST;   // 记录HTTP请求的处理结果

    // 主状态机，用于从buffer总取出所有完整的行
    while ((linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK)
    {
        char *temp = buffer + start_line;
        start_line = checked_index;
        // checkstate记录主状态机当前的状态
        switch (checkstate)
        {
        case CHECK_STATE_RUQUESTLINE: // 第一个状态，分析请求行
        {
            retcode = parse_requestline(temp, checkstate);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER: // 第二个状态，分析头部字段
        {
            retcode = parse_headers(temp);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (retcode == GET_REQUEST)
            {
                return GET_REQUEST;
            }
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }
    // 若没有读取到一个完整的行，则表示还需要继续读取客户数据才能进一步分析
    if (linestatus == LINE_OPEN)
    {
        return NO_REQUEST;
    }
    else
    {
        return BAD_REQUEST;
    }
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ipaddress port \n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd>=0);
    int ret = bind(listenfd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);
    sockaddr_in client_address;
    socklen_t size = sizeof(client_address);
    int fd = accept(listenfd, (sockaddr *)&client_address, &size);
    if (fd < 0)
    {
        printf("errno is:%d", errno);
    }
    else
    {
        char buffer[BUFFER_SIZE];
        memset(buffer, '\0', sizeof(buffer));
        int data_read = 0;
        int read_index = 0;
        int checked_index = 0;
        int start_line = 0;
        // 设置主状态机的初始状态
        CHECK_STATE checkstate = CHECK_STATE_RUQUESTLINE;
        while (1)
        {
            data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);
            if (data_read == -1)
            {
                printf("reading failed\n");
                break;
            }
            else if (data_read == 0)
            {
                printf("remote client has closed the connection\n");
                break;
            }
            read_index += data_read;
            // 分析目前已经获得的所有客户的数据
            HTTP_CODE result = parse_content(buffer, checked_index, checkstate, read_index, start_line);
            if (result == NO_REQUEST) // 尚未得到一个完整的HTTP请求
            {
                continue;
            }
            else if (result == GET_REQUEST) // 得到一个完整的，正确的HTTP请求
            {
                send(fd, szret[0], strlen(szret[1]), 0);
                break;
            }
        }
        close(fd);
    }
    close(listenfd);
    return 0;
}