/*
Copyright (C) 2013 hettoo (Gerco van Heerdt)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "utils.h"
#include "ui.h"
#include "net.h"
#include "cmd.h"
#include "serverlist.h"

#define MAX_SERVERS 2048

#define MAX_TOKEN_SIZE 512

typedef struct server_s {
    char address[32];
    int port;
    int sockfd;
    struct sockaddr_in serv_addr;
    int ping_start;
    int ping_end;

    char name[MAX_TOKEN_SIZE];
    char players[MAX_TOKEN_SIZE];
} server_t;

static char filter[512];

static server_t serverlist[MAX_SERVERS];
static int server_count = 0;

static int sockfd;
static struct sockaddr_in serv_addr;
static socklen_t slen;
static msg_t msg;
static msg_t rmsg;

#define MASTER "64.22.107.125"
#define PORT_MASTER 27950

void serverlist_connect() {
    static char cmd[100];
    int id = atoi(cmd_argv(1));
    if (id >= 0 && id < server_count) {
        sprintf(cmd, "connect %s %d", serverlist[id].address, serverlist[id].port);
        cmd_execute(-2, cmd);
    } else {
        ui_output(-2, "Invalid id.\n");
    }
}

void serverlist_init() {
    sockfd = -1;
    cmd_add_global("list", serverlist_query);
    cmd_add_global("c", serverlist_connect);
}

void serverlist_query() {
    strcpy(filter, cmd_argv(1));
    server_count = 0;
    sockfd = -1;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ui_output(-2, "Unable to create socket.\n");
        return;
    }

    bzero(&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT_MASTER);

    if (inet_aton(MASTER, &serv_addr.sin_addr) == 0) {
        ui_output(-2, "Invalid hostname.\n");
        sockfd = -1;
        return;
    }

    msg_clear(&msg);
    write_long(&msg, -1);
    write_string(&msg, "getservers Warsow 15 full empty");

    slen = sizeof(serv_addr);
    if (sendto(sockfd, msg.data, msg.cursize, 0, (struct sockaddr*)&serv_addr, slen) == -1) {
        ui_output(-2, "sendto failed");
        sockfd = -1;
    }
}

static void ping_server(server_t *server) {
    server->sockfd = -1;
    if ((server->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ui_output(-2, "Unable to create socket.\n");
        return;
    }

    bzero(&server->serv_addr, sizeof(server->serv_addr));

    server->serv_addr.sin_family = AF_INET;
    server->serv_addr.sin_port = htons(server->port);

    if (inet_aton(server->address, &server->serv_addr.sin_addr) == 0) {
        ui_output(-2, "Invalid hostname.\n");
        server->sockfd = -1;
        return;
    }

    msg_clear(&msg);
    write_long(&msg, -1);
    write_string(&msg, "info 15 full empty");

    slen = sizeof(server->serv_addr);
    server->ping_start = millis();
    if (sendto(server->sockfd, msg.data, msg.cursize, 0, (struct sockaddr*)&server->serv_addr, slen) == -1) {
        ui_output(-2, "sendto failed");
        server->sockfd = -1;
    }
}

static server_t *find_server(char *address, int port) {
    int i;
    for (i = 0; i < server_count; i++) {
        if (serverlist[i].port == port && !strcmp(serverlist[i].address, address))
            return serverlist + i;
    }
    return NULL;
}

static void read_server(server_t *server, char *info) {
    int i;
    static char key[MAX_TOKEN_SIZE];
    static char value[MAX_TOKEN_SIZE];
    server->name[0] = '\0';
    server->players[0] = '\0';
    qboolean is_key = qtrue;
    key[0] = '\0';
    int len = strlen(info);
    int o = 0;
    for (i = 0; i < len; i++) {
        if (info[i] == '\\' && info[i + 1] == '\\') {
            value[o] = '\0';
            is_key = !is_key;
            if (is_key) {
                strcpy(key, value);
            } else {
                if (!strcmp(key, "n"))
                    strcpy(server->name, value);
                else if (!strcmp(key, "u"))
                    strcpy(server->players, value);
                key[0] = '\0';
            }
            i++;
            o = 0;
        } else {
            if (o > 0 || info[i] != ' ')
                value[o++] = info[i];
        }
    }
}

void serverlist_frame() {
    int i;
    for (i = 0; i < server_count; i++) {
        if (serverlist[i].sockfd) {
            msg_clear(&rmsg);
            msg_clear(&rmsg);
            if ((rmsg.cursize = recvfrom(serverlist[i].sockfd, rmsg.data, rmsg.maxsize, MSG_DONTWAIT, (struct sockaddr*)&serv_addr, &slen)) == -1) {
                if (errno == EAGAIN)
                    continue;
                die("recvfrom failed");
            }
            serverlist[i].ping_end = millis();
            int seq = read_long(&rmsg);
            if (seq != -1)
                continue;
            skip_data(&rmsg, strlen("info\n"));
            read_server(serverlist + i, read_string(&rmsg));
            if (partial_match(filter, serverlist[i].name))
                ui_output(-2, "^5%i ^7(%i) %s %s\n", i, serverlist[i].ping_end - serverlist[i].ping_start, serverlist[i].players, serverlist[i].name, read_string(&rmsg));
        }
    }
    if (sockfd < 0)
        return;

    msg_clear(&rmsg);
    if ((rmsg.cursize = recvfrom(sockfd, rmsg.data, rmsg.maxsize, MSG_DONTWAIT, (struct sockaddr*)&serv_addr, &slen)) == -1) {
        if (errno == EAGAIN)
            return;
        die("recvfrom failed");
    }
    int seq = read_long(&rmsg);
    if (seq != -1)
        return;

    char address_string[32];
    qbyte address[4];
    unsigned short port;

    skip_data(&rmsg, strlen("getserversResponse"));
	while (rmsg.readcount + 7 <= rmsg.cursize) {
        char prefix = read_char(&rmsg);
        port = 0;

        if (prefix == '\\') {
            read_data(&rmsg, address, 4);
            port = ShortSwap(read_short(&rmsg));
            sprintf(address_string, "%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
        }

        if (port != 0) {
            server_t *server = find_server(address_string, port);
            if (server != NULL)
                continue;
            server = serverlist + server_count++;
            strcpy(server->address, address_string);
            server->port = port;
            ping_server(server);
        }
    }
}
