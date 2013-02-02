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

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
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

    sock_t sock;
    int ping_start;
    int ping_end;

    char name[MAX_TOKEN_SIZE];
    char players[MAX_TOKEN_SIZE];
} server_t;

static char filter[512];

static server_t serverlist[MAX_SERVERS];
static int server_count = 0;

static sock_t sock;

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
    sock_init(&sock);
    cmd_add_global("list", serverlist_query);
    cmd_add_global("c", serverlist_connect);
}

void serverlist_query() {
    strcpy(filter, cmd_argv(1));
    server_count = 0;

    sock_connect(&sock, MASTER, PORT_MASTER);

    msg_t *msg = sock_init_send(&sock, qfalse);
    write_string(msg, "getservers Warsow 15 full empty");
    sock_send(&sock);
}

static void ping_server(server_t *server) {
    sock_connect(&server->sock, server->address, server->port);

    msg_t *msg = sock_init_send(&server->sock, qfalse);
    write_string(msg, "info 15 full empty");
    sock_send(&server->sock);
    server->ping_start = millis();
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
        msg_t *msg = sock_recv(&serverlist[i].sock);
        if (!msg)
            continue;
        serverlist[i].ping_end = millis();
        skip_data(msg, strlen("info\n"));
        read_server(serverlist + i, read_string(msg));
        if (partial_match(filter, serverlist[i].name))
            ui_output(-2, "^5%i ^7(%i) %s %s\n", i, serverlist[i].ping_end - serverlist[i].ping_start, serverlist[i].players, serverlist[i].name, read_string(msg));
    }

    msg_t *msg = sock_recv(&sock);
    if (!msg)
        return;

    char address_string[32];
    qbyte address[4];
    unsigned short port;

    skip_data(msg, strlen("getserversResponse"));
	while (msg->readcount + 7 <= msg->cursize) {
        char prefix = read_char(msg);
        port = 0;

        if (prefix == '\\') {
            read_data(msg, address, 4);
            port = ShortSwap(read_short(msg));
            sprintf(address_string, "%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
        }

        if (port != 0) {
            server_t *server = find_server(address_string, port);
            if (server != NULL)
                continue;
            server = serverlist + server_count++;
            sock_init(&server->sock);
            strcpy(server->address, address_string);
            server->port = port;
            ping_server(server);
        }
    }
}
