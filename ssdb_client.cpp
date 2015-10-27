#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "socketlibtypes.h"

#include "ssdb_client.h"

#ifdef PLATFORM_WINDOWS
#include <MSTcpIP.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include <netinet/tcp.h>
#endif

#if defined PLATFORM_WINDOWS
#define snprintf _snprintf 
#endif

#define DEFAULT_SSDBPROTOCOL_LEN 1024

static const uint KEEP_ALIVE_TIMEOUT = 30;
static const uint KEEP_ALIVE_INTERVAL = 3;
static const uint KEEP_ALIVE_PROBES = 10;

using namespace std;

static void
ox_socket_init(void)
{
#if defined PLATFORM_WINDOWS
    static WSADATA g_WSAData;
    WSAStartup(MAKEWORD(2,2), &g_WSAData);
#endif
}

static bool ox_socket_keepalive(sock socket, uint timeout, uint interval, uint probes)
{
#ifdef PLATFORM_WINDOWS
	tcp_keepalive tcpKeepAlive;
	tcpKeepAlive.onoff = 1;
	tcpKeepAlive.keepalivetime = timeout * 1000;
	tcpKeepAlive.keepaliveinterval = interval * 1000;
	DWORD dwBytesRet = 0; 
	int result = WSAIoctl(
		socket,
		SIO_KEEPALIVE_VALS,
		&tcpKeepAlive,
		sizeof(tcpKeepAlive),
		NULL,
		0,
		&dwBytesRet,
		NULL,
		NULL
		);
	if(result != 0)
	{
		return false;
	}
#else
	int hSocket = (int)socket;
	int enable = 1;
	if(setsockopt(hSocket, SOL_SOCKET, SO_KEEPALIVE, (void *)&enable, sizeof(enable)) != 0)
	{
		return false;
	}
	setsockopt(hSocket, SOL_TCP, TCP_KEEPIDLE, (void *)&timeout, sizeof(timeout));
	setsockopt(hSocket, SOL_TCP, TCP_KEEPINTVL, (void *)&interval, sizeof(interval));
	setsockopt(hSocket, SOL_TCP, TCP_KEEPCNT, (void *)&probes, sizeof(probes));
#endif
	return true;
}

static bool ox_socket_set_block(sock socket, bool block)
{
#ifdef _WIN32
	u_long nonblock = block ? 0 : 1;
	return ioctlsocket(socket, FIONBIO, &nonblock) == 0;
#else
	int flag = fcntl(socket, F_GETFL, 0);
	if(block)
	{
		flag &= (~O_NONBLOCK);
		flag &= (~O_NDELAY);
	}
	else
	{
		flag |= O_NONBLOCK;
		flag |= O_NDELAY;
	}
	return fcntl(socket, F_SETFL, flag) != -1;
#endif
}

static bool ox_socket_set_timeout(sock socket, uint timeoutSec)
{
#ifdef _WIN32
	int timeout = timeoutSec * 1000;
#else
	timeval timeout = { timeoutSec, 0 };
#endif
	if(setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) == SOCKET_ERROR)
	{
		return false;
	}
	if(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == SOCKET_ERROR)
	{
		return false;
	}
	return true;
}

int ox_get_last_error()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

static void
ox_socket_close(sock fd)
{
#if defined PLATFORM_WINDOWS
    closesocket(fd);
#else
    close(fd);
#endif
}

static sock
ox_socket_connect(const char* server_ip, int port, uint timeoutSec=10)
{
    struct sockaddr_in server_addr;
    sock clientfd = SOCKET_ERROR;

    ox_socket_init();

    clientfd = socket(AF_INET, SOCK_STREAM, 0);
	if (clientfd == SOCKET_ERROR)
	{
		return clientfd;
	}
	if (!ox_socket_set_block(clientfd, false))
	{
		ox_socket_close(clientfd);
		return SOCKET_ERROR;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server_ip);
	server_addr.sin_port = htons(port);

	if (connect(clientfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
	{
		int error = ox_get_last_error();
#ifdef PLATFORM_WINDOWS
		if (error != WSAEWOULDBLOCK)
#else
		if (error != EINPROGRESS)
#endif
		{
			ox_socket_close(clientfd);
			return SOCKET_ERROR;
		}
		fd_set fdsWrite;
		FD_ZERO(&fdsWrite);
		FD_SET(clientfd, &fdsWrite);
		fd_set fdsExcept;
		FD_ZERO(&fdsExcept);
		FD_SET(clientfd, &fdsExcept);
		timeval timeout = { timeoutSec, 0 };
#ifdef PLATFORM_WINDOWS
		int ret = select(0, NULL, &fdsWrite, &fdsExcept, &timeout);
#else
		int ret = select(clientfd + 1, NULL, &fdsWrite, &fdsExcept, &timeout);
#endif
		if(ret > 0)
		{
#ifdef PLATFORM_WINDOWS
			int errorLength = sizeof(error);
#else
			socklen_t errorLength = sizeof(error);
#endif
			if(getsockopt(clientfd, SOL_SOCKET, SO_ERROR, (char *)&error, &errorLength) == SOCKET_ERROR)
			{
				ox_socket_close(clientfd);
				return SOCKET_ERROR;
			}
			if(error != 0)
			{
				ox_socket_close(clientfd);
				return SOCKET_ERROR;
			}
		}
		else if(ret == 0)
		{
			ox_socket_close(clientfd);
			return SOCKET_ERROR;
		}
		else
		{
			ox_socket_close(clientfd);
			return SOCKET_ERROR;
		}
	}
	if (!ox_socket_set_block(clientfd, true))
	{
		ox_socket_close(clientfd);
		return SOCKET_ERROR;
	}
	if (!ox_socket_set_timeout(clientfd, timeoutSec))
	{
		ox_socket_close(clientfd);
		return SOCKET_ERROR;
	}

    return clientfd;
}

static int
ox_socket_nodelay(sock fd)
{
    int flag = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

class SSDBProtocolRequest
{
public:
    SSDBProtocolRequest()
    {
        m_request = ox_buffer_new(DEFAULT_SSDBPROTOCOL_LEN);
    }

    ~SSDBProtocolRequest()
    {
        ox_buffer_delete(m_request);
        m_request = NULL;
    }

    void appendStr(const char* str)
    {
        int len = (int)strlen(str);
        char lenstr[16];
        int num = snprintf(lenstr, sizeof(len), "%d\n", len);
        appendBlock(lenstr, num);
        appendBlock(str, len);
        appendBlock("\n", 1);
    }

    void appendInt64(int64_t val)
    {
        char str[30];
        snprintf(str, sizeof(str), "%lld", val);
        appendStr(str);
    }

	void appendInt32(int val)
	{
		char str[16];
		snprintf(str, sizeof(str), "%d", val);
		appendStr(str);
	}

    void appendStr(const string& str)
    {
        char len[16];
        int num = snprintf(len, sizeof(len), "%d\n", (int)str.size());
        appendBlock(len, num);
        appendBlock(str.c_str(), (int)str.length());
        appendBlock("\n", 1);
    }

    void endl()
    {
        appendBlock("\n", 1);
    }

    void appendBlock(const char* data, int len)
    {
        if (ox_buffer_getwritevalidcount(m_request) < len)
        {
            buffer_s* temp = ox_buffer_new(ox_buffer_getsize(m_request) + len);
            memcpy(ox_buffer_getwriteptr(temp), ox_buffer_getreadptr(m_request), ox_buffer_getreadvalidcount(m_request));
            ox_buffer_addwritepos(temp, ox_buffer_getreadvalidcount(m_request));
            ox_buffer_delete(m_request);
            m_request = temp;
        }

        ox_buffer_write(m_request, data, len);
    }

    const char* getResult()
    {
        return ox_buffer_getreadptr(m_request);
    }
    int getResultLen()
    {
        return ox_buffer_getreadvalidcount(m_request);
    }

    void init()
    {
        ox_buffer_init(m_request);
    }
private:
    buffer_s*   m_request;
};

struct Bytes
{
    const char* buffer;
    int len;
};

class SSDBProtocolResponse
{
public:
    ~SSDBProtocolResponse()
    {
    }

    void init()
    {
        mBuffers.clear();
    }

    void parse(const char* buffer, int len)
    {
        const char* current = buffer;
        while (true)
        {
            char* temp;
            int datasize = strtol(current, &temp, 10);
            current = temp;
            current += 1;
            Bytes tmp = { current, datasize };
            mBuffers.push_back(tmp);
            current += datasize;

            current += 1;

            if (*current == '\n')
            {
                /*  收到完整消息,ok  */
                current += 1;         /*  跳过\n    */
                break;
            }
        }
    }

    Bytes* getByIndex(size_t index)
    {
        if(mBuffers.size() > index)
        {
            return &mBuffers[index];
        }
        else
        {
            const char* nullstr = "null";
            static  Bytes nullbuffer = { nullstr, (int)strlen(nullstr)+1 };
            return &nullbuffer;
        }
    }

    size_t getBuffersLen() const
    {
        return mBuffers.size();
    }

    Status getStatus()
    {
        if(mBuffers.empty())
        {
            return Status("error");
        }

        return string(mBuffers[0].buffer, mBuffers[0].len);
    }

    static int check_ssdb_packet(const char* buffer, int len)
    {
        const char* end = buffer + len; /*  无效内存地址  */
        const char* current = buffer;   /*  当前解析位置*/

        while (true)
        {
            char* temp;
            int datasize = strtol(current, &temp, 10);
            if (datasize == 0 && temp == current)
            {
                break;
            }
            current = temp;         /*  跳过datasize*/

            if (current >= end || *current != '\n')
            {
                break;
            }
            current += 1;         /*  跳过\n    */
            current += datasize;  /*  跳过data  */

            if (current >= end || *current != '\n')
            {
                break;
            }

            current += 1;         /*  跳过\n    */

            if (current >= end)
            {
                break;
            }
            else if(*current == '\n')
            {
                /*  收到完整消息,ok  */
                current += 1;         /*  跳过\n    */
                return (int)(current - buffer);
            }
        }

        /*  非完整消息返回0  */
        return 0;
    }

private:
    vector<Bytes>   mBuffers;
};

static Status read_list(SSDBProtocolResponse *response, std::vector<std::string> *ret)
{
    Status status = response->getStatus();
    if(status.ok())
    {
        for (size_t i = 1; i < response->getBuffersLen(); ++i)
        {
            Bytes* buffer = response->getByIndex(i);
			ret->push_back(std::string(buffer->buffer, buffer->len));
        }
    }

    return status;
}

static Status read_map(SSDBProtocolResponse *response, std::map<std::string, std::string> *ret)
{
	Status s = response->getStatus();
	if (s.ok())
	{
		for (size_t i = 1; i < response->getBuffersLen(); i += 2)
		{
			Bytes *key = response->getByIndex(i);
			Bytes *value = response->getByIndex(i+1);
			ret->insert(std::make_pair(std::string(key->buffer, key->len), std::string(value->buffer, value->len)));
		}
	}
	return s;
}

static Status read_int64(SSDBProtocolResponse *response, int64_t *ret)
{
    Status status = response->getStatus();
    if(status.ok())
    {
        if(response->getBuffersLen() >= 2)
        {
            Bytes* buf = response->getByIndex(1);
            string temp(buf->buffer, buf->len);
            sscanf(temp.c_str(), "%lld",ret);
        }
        else
        {
            status = Status("server_error");
        }
    }

    return status;
}

static Status read_int(SSDBProtocolResponse *response, int *ret)
{
	Status s = response->getStatus();
	if (s.ok())
	{
		if (response->getBuffersLen() >= 2)
		{
			Bytes* buf = response->getByIndex(1);
			string temp(buf->buffer, buf->len);
			sscanf(temp.c_str(), "%d", ret);
		}
		else
		{
			s = Status("server_error");
		}
	}
	return s;
}

static Status read_str(SSDBProtocolResponse *response, std::string *ret)
{
    Status status = response->getStatus();
    if(status.ok())
    {
        if(response->getBuffersLen() >= 2)
        {
            Bytes* buf = response->getByIndex(1);
            *ret = string(buf->buffer, buf->len);
        }
        else
        {
            status = Status("server_error");
        }
    }

    return status;
}

void SSDBClient::request(const char* buffer, int len)
{
	if (!isconnected())
	{
		disconnect();
		connect(m_ip.c_str(), m_port, m_timeout);
	}
    m_reponse->init();

    int left_len = send(buffer, len);

    /*  如果发送请求完毕，就进行接收response处理    */
    if(len > 0 && left_len == 0)
    {
        recv();
    }

    m_request->init();
}

int SSDBClient::send(const char* buffer, int len)
{
    int left_len = len;
    while(m_socket != SOCKET_ERROR && left_len > 0)
    {
        int sendret = ::send(m_socket, buffer+(len-left_len), left_len, 0);
        if(sendret < 0)
        {
            if(sErrno != S_EINTR && sErrno != S_EWOULDBLOCK)
            {
                /*  链接断开    */
                ox_socket_close(m_socket);
                m_socket = SOCKET_ERROR;
                break;
            }
        }
        else
        {
            left_len -= sendret;
        }
    }

    return left_len;
}

void SSDBClient::recv()
{
    /*  重置读缓冲区  */
    ox_buffer_init(m_recvBuffer);
    while(m_socket != SOCKET_ERROR)
    {
        if(ox_buffer_getwritevalidcount(m_recvBuffer) < 128)
        {
            /*  扩大缓冲区   */
            buffer_s* temp = ox_buffer_new(ox_buffer_getsize(m_recvBuffer) + 1024);
            memcpy(ox_buffer_getwriteptr(temp), ox_buffer_getreadptr(m_recvBuffer), ox_buffer_getreadvalidcount(m_recvBuffer));
            ox_buffer_addwritepos(temp, ox_buffer_getreadvalidcount(m_recvBuffer));
            ox_buffer_delete(m_recvBuffer);
            m_recvBuffer = temp;
        }

        int len = ::recv(m_socket, ox_buffer_getwriteptr(m_recvBuffer), ox_buffer_getwritevalidcount(m_recvBuffer), 0);
        if((len == -1 && sErrno != S_EINTR && sErrno != S_EWOULDBLOCK) || len == 0)
        {
            ox_socket_close(m_socket);
            m_socket = SOCKET_ERROR;
            break;
        }
        else if(len > 0)
        {
            ox_buffer_addwritepos(m_recvBuffer, len);

            /*  尝试解析,返回值大于0表示接受到完整的response消息包    */
            if(SSDBProtocolResponse::check_ssdb_packet(ox_buffer_getreadptr(m_recvBuffer), ox_buffer_getreadvalidcount(m_recvBuffer)) > 0)
            {
                m_reponse->parse(ox_buffer_getreadptr(m_recvBuffer), ox_buffer_getreadvalidcount(m_recvBuffer));
                break;
            }
        }
    }
}

SSDBClient::SSDBClient()
{
    ox_socket_init();
    m_reponse = new SSDBProtocolResponse;
    m_request = new SSDBProtocolRequest;
    m_socket = SOCKET_ERROR;
    m_recvBuffer = ox_buffer_new(DEFAULT_SSDBPROTOCOL_LEN);
}

SSDBClient::~SSDBClient()
{
    if(m_socket != SOCKET_ERROR)
    {
        ox_socket_close(m_socket);
        m_socket = SOCKET_ERROR;
    }
    if(m_reponse != NULL)
    {
        delete m_reponse;
        m_reponse = NULL;
    }
    if (m_request != NULL)
    {
        delete m_request;
        m_request = NULL;
    }
    if(m_recvBuffer != NULL)
    {
        ox_buffer_delete(m_recvBuffer);
        m_recvBuffer = NULL;
    }

#if defined PLATFORM_WINDOWS
    WSACleanup();
#endif
}

void SSDBClient::disconnect()
{
    if (m_socket != SOCKET_ERROR)
    {
        ox_socket_close(m_socket);
        m_socket = SOCKET_ERROR;
    }
}

void SSDBClient::connect(const char* ip, int port, uint timeoutSec)
{
    if(m_socket == SOCKET_ERROR)
    {
        m_socket = (int)ox_socket_connect(ip, port, timeoutSec);
        if(m_socket != SOCKET_ERROR)
        {
            ox_socket_nodelay(m_socket);
			ox_socket_keepalive(m_socket, KEEP_ALIVE_TIMEOUT, KEEP_ALIVE_INTERVAL, KEEP_ALIVE_PROBES);
        }

        m_ip = ip;
        m_port = port;
		m_timeout = timeoutSec;
    }
}

bool SSDBClient::isconnected() const
{
    return m_socket != SOCKET_ERROR;
}

void SSDBClient::execute(const char* str, int len)
{
    request(str, len);
}

Status SSDBClient::set(const std::string& key, const std::string& val)
{
    m_request->appendStr("set");
    m_request->appendStr(key);
    m_request->appendStr(val);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());
    return m_reponse->getStatus();
}

Status SSDBClient::setx(const std::string& key, const std::string& val, int ttl)
{
	m_request->appendStr("setx");
	m_request->appendStr(key);
	m_request->appendStr(val);
	m_request->appendInt32(ttl);
	m_request->endl();

	request(m_request->getResult(), m_request->getResultLen());
	return m_reponse->getStatus();
}

Status SSDBClient::setnx(const std::string& key, const std::string& val, int *reply)
{
	m_request->appendStr("setnx");
	m_request->appendStr(key);
	m_request->appendStr(val);
	m_request->endl();
	request(m_request->getResult(), m_request->getResultLen());
	return read_int(m_reponse, reply);
}

Status SSDBClient::get(const std::string& key, std::string *val)
{
    m_request->appendStr("get");
    m_request->appendStr(key);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_str(m_reponse, val);
}

Status SSDBClient::del(const std::string& key)
{
	m_request->appendStr("del");
	m_request->appendStr(key);
	m_request->endl();

	request(m_request->getResult(), m_request->getResultLen());

	return m_reponse->getStatus();
}

Status SSDBClient::multi_get(const std::vector<std::string>& keys, std::map<std::string, std::string> *ret)
{
	m_request->appendStr("multi_get");
	for (size_t i = 0; i < keys.size(); i++)
	{
		m_request->appendStr(keys[i]);
	}
	m_request->endl();
	request(m_request->getResult(), m_request->getResultLen());
	return read_map(m_reponse, ret);
}

Status SSDBClient::multi_set(const std::map<std::string, std::string>& kvs)
{
	m_request->appendStr("multi_set");
	for (std::map<std::string, std::string>::const_iterator iter = kvs.begin(); iter != kvs.end(); ++iter)
	{
		m_request->appendStr(iter->first);
		m_request->appendStr(iter->second);
	}
	m_request->endl();
	request(m_request->getResult(), m_request->getResultLen());
	return m_reponse->getStatus();
}

Status SSDBClient::multi_del(const std::vector<std::string>& keys)
{
	m_request->appendStr("multi_del");
	for (size_t i = 0; i < keys.size(); i++)
	{
		m_request->appendStr(keys[i]);
	}
	m_request->endl();
	request(m_request->getResult(), m_request->getResultLen());
	return m_reponse->getStatus();
}

Status SSDBClient::hset(const std::string& name, const std::string& key, std::string val)
{
    m_request->appendStr("hset");
    m_request->appendStr(name);
    m_request->appendStr(key);
    m_request->appendStr(val);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return m_reponse->getStatus();
}

Status SSDBClient::multi_hset(const std::string& name, const std::map<std::string, std::string> &kvs)
{
    m_request->appendStr("multi_hset");
    m_request->appendStr(name);
	for (std::map<std::string, std::string>::const_iterator iter = kvs.begin(); iter != kvs.end(); ++iter)
    {
        m_request->appendStr(iter->first);
        m_request->appendStr(iter->second);
    }
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return m_reponse->getStatus();
}

Status SSDBClient::hget(const std::string& name, const std::string& key, std::string *val)
{
    m_request->appendStr("hget");
    m_request->appendStr(name);
    m_request->appendStr(key);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_str(m_reponse, val);
}

Status SSDBClient::multi_hget(const std::string& name, const std::vector<std::string> &keys, std::map<std::string, std::string> *ret)
{
    m_request->appendStr("multi_hget");
    m_request->appendStr(name);
    for (size_t i = 0; i < keys.size(); i++)
    {
        m_request->appendStr(keys[i]);
    }
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_map(m_reponse, ret);
}

Status SSDBClient::zset(const std::string& name, const std::string& key, int64_t score)
{
    m_request->appendStr("zset");
    m_request->appendStr(name);
    m_request->appendStr(key);
    char s_str[30];
    sprintf(s_str, "%lld", score);
    m_request->appendStr(s_str);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return m_reponse->getStatus();
}

Status SSDBClient::zget(const std::string& name, const std::string& key, int64_t *score)
{
    m_request->appendStr("zget");
    m_request->appendStr(name);
    m_request->appendStr(key);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_int64(m_reponse, score);
}

Status SSDBClient::zsize(const std::string& name, int64_t *size)
{
    m_request->appendStr("zsize");
    m_request->appendStr(name);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_int64(m_reponse, size);
}

Status SSDBClient::zkeys(const std::string& name, const std::string& key_start,
    int64_t score_start, int64_t score_end,uint64_t limit, std::vector<std::string> *ret)
{
    m_request->appendStr("zkeys");
    m_request->appendStr(name);
    m_request->appendStr(key_start);
    m_request->appendInt64(score_start);
    m_request->appendInt64(score_end);

    char buf[30] = {0};
    snprintf(buf, sizeof(buf), "%llu", limit);
    m_request->appendStr(buf);

    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_list(m_reponse, ret);
}

Status SSDBClient::zscan(const std::string& name, const std::string& key_start,
    int64_t score_start, int64_t score_end,uint64_t limit, std::vector<std::string> *ret)
{
    m_request->appendStr("zscan");
    m_request->appendStr(name);
    m_request->appendStr(key_start);
    m_request->appendInt64(score_start);
    m_request->appendInt64(score_end);

    char buf[30] = {0};
    snprintf(buf, sizeof(buf), "%llu", limit);
    m_request->appendStr(buf);

    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_list(m_reponse, ret);
}

Status SSDBClient::zclear(const std::string& name)
{
    m_request->appendStr("zclear");
    m_request->appendStr(name);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return m_reponse->getStatus();
}

Status SSDBClient::qpush(const std::string& name, const std::string& item)
{
    m_request->appendStr("qpush");
    m_request->appendStr(name);
    m_request->appendStr(item);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());
    return m_reponse->getStatus();
}

Status SSDBClient::qpop(const std::string& name, std::string* item)
{
    m_request->appendStr("qpop");
    m_request->appendStr(name);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_str(m_reponse, item);
}

Status SSDBClient::qslice(const std::string& name, int64_t begin, int64_t end, std::vector<std::string> *ret)
{
    m_request->appendStr("qslice");
    m_request->appendStr(name);
    m_request->appendInt64(begin);
    m_request->appendInt64(end);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());

    return read_list(m_reponse, ret);
}

Status SSDBClient::qclear(const std::string& name)
{
    m_request->appendStr("qclear");
    m_request->appendStr(name);
    m_request->endl();

    request(m_request->getResult(), m_request->getResultLen());
    return m_reponse->getStatus();
}
