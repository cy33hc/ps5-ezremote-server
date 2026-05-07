#ifndef EZ_HTTP_SERVER_H
#define EZ_HTTP_SERVER_H

#include "http/httplib.h"

using namespace httplib;
extern Server *svr;

extern int http_server_port;

namespace HttpServer
{
    void *ServerThread(void *argp);
    void Start();
    void Stop();
}

#endif