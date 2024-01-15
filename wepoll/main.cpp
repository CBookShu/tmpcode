// wepoll.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "wepoll.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include <iostream>
#include <string_view>
#include <thread>
#include <cassert>
#include <format>

static int SendBuff(SOCKET s, const char* buf, int size) {
    int cur = 0;
    while (cur < size) {
        int rc = ::send(s, buf + cur, size - cur, 0);
        if (rc < 0) {
            if (WSAEINTR == WSAGetLastError()) {
                continue;
            }
            break;
        }
        cur += rc;
    }
    return cur;
}

static std::string ReadBuf(SOCKET s, int size) {
    std::string data; data.resize(size);

    do {
        int rc = ::recv(s, data.data(), size, 0);
        if (rc < 0 && WSAEINTR == WSAGetLastError()) {
            continue;
        }
        if (rc > 0) {
            data.resize(rc);
        }
    } while (0);
    return data;
}

int main()
{
    std::int16_t port = 9595;

    auto fd = epoll_create(10);

    // server 代码
    auto h_server = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(h_server != INVALID_SOCKET);

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    auto rc = ::bind(h_server, (struct sockaddr*)&addr, sizeof(addr));
    assert(rc != SOCKET_ERROR);

    rc = ::listen(h_server, SOMAXCONN);
    assert(rc != SOCKET_ERROR);


    // client 代码
    auto h_client = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(h_client != INVALID_SOCKET);

    ::inet_pton(AF_INET, "127.0.0.1", &(addr.sin_addr));
    rc = ::connect(h_client, (struct sockaddr*)&addr, sizeof(addr));
    assert(rc != SOCKET_ERROR);

    int poll_size = 2;
    struct epoll_event event[3];
    event[0].data.sock = h_server;
    event[0].events = EPOLLIN;

    event[1].data.sock = h_client;
    event[1].events = EPOLLIN;

    for (int i = 0; i < poll_size; ++i) {
        auto r = epoll_ctl(fd, EPOLL_CTL_ADD, event[i].data.sock, event + i);
        assert(r != -1);
    }

    SOCKET h_session = INVALID_SOCKET;
    for (;;) {
        int count = epoll_wait(fd, event, poll_size, -1);
        for (int i = 0; i < count; ++i) {
            if (event[0].events & EPOLLIN) {
                if (event[0].data.sock == h_server) {
                    // 服务端监听到client
                    std::cout << std::format("server get:") << std::endl;

                    if (h_session != INVALID_SOCKET) {
                        epoll_ctl(fd, EPOLL_CTL_DEL, event[2].data.sock, event + 2);
                        closesocket(h_session);
                    }

                    struct sockaddr_in addr_client;
                    int len = sizeof(addr_client);
                    h_session = ::accept(h_server, (struct sockaddr*)&addr_client, &len);
                    if (h_session != INVALID_SOCKET) {
                        event[2].events = EPOLLIN;
                        event[2].data.sock = h_session;
                        epoll_ctl(fd, EPOLL_CTL_ADD, event[2].data.sock, event + 2);
                        poll_size++;

                        // 直接测试发消息
                        std::string_view buf("hello world");
                        SendBuff(h_session, buf.data(), buf.size());
                    }
                }
                else if (event[0].data.sock == h_client) {
                    std::cout << std::format("client get:") << std::endl;

                    auto buf = ReadBuf(h_client, 32);
                    std::cout << buf << std::endl;

                    SendBuff(h_client, buf.data(), buf.size());
                }
                else if (event[0].data.sock == h_session) {
                    // server accept 拿到的session
                    std::cout << std::format("session get:") << std::endl;

                    auto buf = ReadBuf(h_session, 32);
                    std::cout << buf << std::endl;
                }
            }
        }
    }

    if (h_server != INVALID_SOCKET) {
        closesocket(h_server);
    }
    if (h_client != INVALID_SOCKET) {
        closesocket(h_client);
    }
    if (h_session != INVALID_SOCKET) {
        closesocket(h_session);
    }

    epoll_close(fd);
    return 0;
}
