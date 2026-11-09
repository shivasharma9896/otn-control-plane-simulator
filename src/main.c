#define _GNU_SOURCE
#include "node_daemon.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void on_signal(int sig){(void)sig;stopping=1;}
static void usage(const char *p){printf("usage: %s [-c config] [-i node-id] [-p control-port] [-d discovery-port] [-b bind-address] [-D discovery-address] [-m max-peers] [-I interval-ms] [-T timeout-ms] [-l level]\n",p);}
static int parse_uint(const char *text, unsigned long max, unsigned *out) {
    char *end = NULL; unsigned long value = strtoul(text, &end, 10);
    if (!text[0] || !end || *end || value > max) return -1;
    *out = (unsigned)value; return 0;
}
static void trim(char **text) {
    while (**text == ' ' || **text == '\t') (*text)++;
    char *end = *text + strlen(*text);
    while (end > *text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n')) *--end = '\0';
}
static int apply_setting(node_daemon_config_t *c, const char *key, const char *value) {
    unsigned v;
    if (!strcmp(key, "node_id")) return parse_uint(value, UINT32_MAX, &v) ? -1 : (c->node_id = v, 0);
    if (!strcmp(key, "control_port")) return parse_uint(value, 65535, &v) ? -1 : (c->control_port = (uint16_t)v, 0);
    if (!strcmp(key, "discovery_port")) return parse_uint(value, 65535, &v) ? -1 : (c->discovery_port = (uint16_t)v, 0);
    if (!strcmp(key, "heartbeat_interval_ms")) return parse_uint(value, 3600000, &v) ? -1 : (c->heartbeat_interval_ms = v, 0);
    if (!strcmp(key, "heartbeat_timeout_ms")) return parse_uint(value, 3600000, &v) ? -1 : (c->heartbeat_timeout_ms = v, 0);
    if (!strcmp(key, "reconnect_base_ms")) return parse_uint(value, 3600000, &v) ? -1 : (c->reconnect_base_ms = v, 0);
    if (!strcmp(key, "reconnect_max_ms")) return parse_uint(value, 3600000, &v) ? -1 : (c->reconnect_max_ms = v, 0);
    if (!strcmp(key, "max_peers")) return parse_uint(value, 64, &v) ? -1 : (c->max_peers = v, 0);
    if (!strcmp(key, "bind_address")) return snprintf(c->bind_address, sizeof(c->bind_address), "%s", value) >= (int)sizeof(c->bind_address) ? -1 : 0;
    if (!strcmp(key, "discovery_address")) return snprintf(c->discovery_address, sizeof(c->discovery_address), "%s", value) >= (int)sizeof(c->discovery_address) ? -1 : 0;
    if (!strcmp(key, "log_level")) return log_level_parse(value, &c->log_level);
    return -1;
}
static int load_config(const char *path, node_daemon_config_t *c) {
    FILE *file = fopen(path, "r"); if (!file) return -1;
    char line[256]; unsigned line_no = 0;
    while (fgets(line, sizeof(line), file)) {
        line_no++; char *text = line; trim(&text);
        if (!*text || *text == '#') continue;
        char *equals = strchr(text, '='); if (!equals) { fclose(file); errno = EINVAL; return -1; }
        *equals++ = '\0'; char *key = text; char *value = equals; trim(&key); trim(&value);
        if (apply_setting(c, key, value) < 0) { fclose(file); errno = EINVAL; return -1; }
    }
    (void)line_no; fclose(file); return 0;
}
int main(int argc,char **argv){
    node_daemon_config_t config; node_daemon_config_defaults(&config); config.node_id = (uint32_t)getpid();
    const char *config_path = NULL; int c;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "-c")) config_path = argv[i + 1];
    if (config_path && load_config(config_path, &config) < 0) { perror("config"); return 2; }
    while((c=getopt(argc,argv,"c:i:p:d:b:D:m:I:T:l:h"))!=-1){
        unsigned value;
        switch(c){
        case 'c': break;
        case 'i': if(parse_uint(optarg, UINT32_MAX, &value)) return 2; config.node_id=value; break;
        case 'p': if(parse_uint(optarg, 65535, &value)) return 2; config.control_port=(uint16_t)value; break;
        case 'd': if(parse_uint(optarg, 65535, &value)) return 2; config.discovery_port=(uint16_t)value; break;
        case 'b': if(snprintf(config.bind_address,sizeof(config.bind_address),"%s",optarg) >= (int)sizeof(config.bind_address)) return 2; break;
        case 'D': if(snprintf(config.discovery_address,sizeof(config.discovery_address),"%s",optarg) >= (int)sizeof(config.discovery_address)) return 2; break;
        case 'm': if(parse_uint(optarg, 64, &value) || value == 0) return 2; config.max_peers=value; break;
        case 'I': if(parse_uint(optarg, 3600000, &value) || value == 0) return 2; config.heartbeat_interval_ms=value; break;
        case 'T': if(parse_uint(optarg, 3600000, &value) || value == 0) return 2; config.heartbeat_timeout_ms=value; break;
        case 'l': if(log_level_parse(optarg, &config.log_level)) return 2; break;
        default: usage(argv[0]); return c=='h'?0:2;
        }
    }
    log_set_level(config.log_level);
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGPIPE,SIG_IGN);
    node_daemon_t*n=node_daemon_create_with_config(&config);if(!n||node_daemon_start(n)<0){perror("start");node_daemon_destroy(n);return 1;}
    LOGI("main", "commands: status peers stats help exit");
    (void)stopping;
    (void)node_daemon_run(n);
    node_daemon_stop(n);node_daemon_destroy(n);return 0;
}
