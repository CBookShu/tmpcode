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

    epoll_close(fd);
    return 0;
}
