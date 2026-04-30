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

int main(int argc, char *argv[])
{
    CONFIG::LoadPackageInstallHostData();
    HttpServer::Start();
    Util::Notify("ezRemote Server stopped.");

    return 0;
}
