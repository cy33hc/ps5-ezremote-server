#undef main

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "server/http_server.h"
#include "config.h"
#include "util.h"
//#include "dbglogger.h"

int main(int argc, char *argv[])
{
    //dbglogger_init();
    //dbglogger_log("If you see this you've set up dbglogger correctly.");

    CONFIG::LoadPackageInstallHostData();
    CONFIG::LoadBgDownloadData();

    if (HttpServer::IsStarted())
    {
        Util::Notify("ezRemote Server already started");
        return 0;
    }

    HttpServer::StartDownloadThread();
    HttpServer::Start();
    HttpServer::StopDownloadThread();
    Util::Notify("ezRemote Server stopped.");

    return 0;
}
