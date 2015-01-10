/*
 * Copyright (C) 2011, 2012, 2013 Citrix Systems
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <ifaddrs.h>
#include <getopt.h>
#include <locale.h>
#include <libgen.h>

#include <pthread.h>

#include <signal.h>

#include "libtelnet.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>

#include "userdb.h"
#include "mainrelay.h"

#include "ns_turn_utils.h"

#include "ns_turn_server.h"
#include "ns_turn_maps.h"

#include "apputils.h"

#include "turncli.h"

///////////////////////////////

struct cli_server cliserver;

int use_cli = 1;

ioa_addr cli_addr;
int cli_addr_set = 0;

int cli_port = CLI_DEFAULT_PORT;

char cli_password[CLI_PASSWORD_LENGTH] = "";

int cli_max_output_sessions = DEFAULT_CLI_MAX_OUTPUT_SESSIONS;

///////////////////////////////

struct cli_session {
	evutil_socket_t fd;
	int auth_completed;
	size_t cmds;
	struct bufferevent *bev;
	ioa_addr addr;
	telnet_t *ts;
	FILE* f;
};

///////////////////////////////

#define CLI_PASSWORD_TRY_NUMBER (5)

static const char *CLI_HELP_STR[] = 
  {"",
   "  ?, h, help - print this text",
   "",
   "  quit, q, exit, bye - end CLI session",
   "",
   "  stop, shutdown, halt - shutdown TURN Server",
   "",
   "  pc - print configuration",
   "",
   "  tc <param-name> - toggle a configuration parameter",
   "     (see pc command output for togglable param names)",
   "",
   "  cc <param-name> <param-value> - change a configuration parameter",
   "     (see pc command output for changeable param names)",
   "",
   "  ps [username] - print sessions, with optional exact user match",
   "",
   "  psp usernamestr - print sessions, with partial user string match",
   "",
   "  psd <file-name> - dump ps command output into file on the TURN server system",
   "",
   "  pu [udp|tcp|dtls|tls]- print current users",
   "",
   "  lr - log reset",
   "",
   "  aas ip[:port} - add an alternate server reference",
   "  das ip[:port] - delete an alternate server reference",
   "  atas ip[:port] - add a TLS alternate server reference",
   "  dtas ip[:port] - delete a TLS alternate server reference",
   "",
   "  cs <session-id> - cancel session, forcefully"
   "",
   NULL};

static const char *CLI_GREETING_STR[] = {
  "TURN Server",
  "rfc5766-turn-server",
  TURN_SOFTWARE,
  NULL};

static char CLI_CURSOR[] = "> ";

static const telnet_telopt_t cli_telopts[] = {
    { TELNET_TELOPT_ECHO,      TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_TTYPE,     TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_ZMP,       TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_BINARY,    TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_NAWS,      TELNET_WONT, TELNET_DONT },
    { -1, 0, 0 }
  };

struct toggleable_command {
	const char *cmd;
	vintp data;
};

struct toggleable_command tcmds[] = {
				{"stale-nonce",&turn_params.stale_nonce},
				{"stun-only",&turn_params.stun_only},
				{"no-stun",&turn_params.no_stun},
				{"secure-stun",&turn_params.secure_stun},
				{"no-udp-relay",&turn_params.no_udp_relay},
				{"no-tcp-relay",&turn_params.no_tcp_relay},
				{"no-multicast-peers",&turn_params.no_multicast_peers},
				{"no-loopback-peers",&turn_params.no_loopback_peers},
				{"mobility",&turn_params.mobility},
				{NULL,NULL}
};

struct changeable_command {
	const char *cmd;
	vintp data;
};

struct changeable_command ccmds[] = {
				{"total-quota",NULL},
				{"user-quota",NULL},
				{"cli-max-output-sessions",(vintp)&cli_max_output_sessions},
				{NULL,NULL}
};

///////////////////////////////

static void myprintf(struct cli_session *cs, const char *format, ...)
{
	if(cs && format) {
		va_list args;
		va_start (args, format);
		if(cs->f) {
			vfprintf(cs->f, format, args);
		} else {
			telnet_vprintf(cs->ts, format, args);
		}
		va_end (args);
	}
}

static void log_reset(struct cli_session* cs)
{
	if(cs) {
	  reset_rtpprintf();
	  myprintf(cs,"  log reset done\n");
	}
}

static void print_str_array(struct cli_session* cs, const char** sa)
{
  if(cs && sa) {
    int i=0;
    while(sa[i]) {
      myprintf(cs,"%s\n",sa[i]);
      i++;
    }
  }
}

static const char* get_flag(int val)
{
	if(val)
		return "ON";
	return "OFF";
}

static void cli_print_flag(struct cli_session* cs, int flag, const char* name, int changeable)
{
	if(cs && cs->ts && name) {
		const char *sc="";
		if(changeable)
			sc=" (*)";
		myprintf(cs,"  %s: %s%s\n",name,get_flag(flag),sc);
	}
}

static void cli_print_uint(struct cli_session* cs, unsigned long value, const char* name, int changeable)
{
	if(cs && cs->ts && name) {
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		myprintf(cs,"  %s: %lu%s\n",name,value,sc);
	}
}

static void cli_print_str(struct cli_session* cs, const char *value, const char* name, int changeable)
{
	if(cs && cs->ts && name && value) {
		if(value[0] == 0)
			value="empty";
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		myprintf(cs,"  %s: %s%s\n",name,value,sc);
	}
}

static void cli_print_addr(struct cli_session* cs, ioa_addr *value, int use_port, const char* name, int changeable)
{
	if(cs && cs->ts && name && value) {
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		char s[256];
		if(!use_port)
			addr_to_string_no_port(value,(u08bits*)s);
		else
			addr_to_string(value,(u08bits*)s);
		myprintf(cs,"  %s: %s%s\n",name,s,sc);
	}
}

static void cli_print_addr_list(struct cli_session* cs, turn_server_addrs_list_t *value, int use_port, const char* name, int changeable)
{
	if(cs && cs->ts && name && value && value->size && value->addrs) {
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		char s[256];
		size_t i;
		for(i=0;i<value->size;i++) {
			if(!use_port)
				addr_to_string_no_port(&(value->addrs[i]),(u08bits*)s);
			else
				addr_to_string(&(value->addrs[i]),(u08bits*)s);
			myprintf(cs,"  %s: %s%s\n",name,s,sc);
		}
	}
}

static void cli_print_str_array(struct cli_session* cs, char **value, size_t sz, const char* name, int changeable)
{
	if(cs && cs->ts && name && value && sz) {
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		size_t i;
		for(i=0;i<sz;i++) {
			if(value[i])
				myprintf(cs,"  %s: %s%s\n",name,value[i],sc);
		}
	}
}

static void cli_print_ip_range_list(struct cli_session* cs, ip_range_list_t *value, const char* name, int changeable)
{
	if(cs && cs->ts && name && value && value->ranges_number && value->ranges) {
		const char *sc="";
		if(changeable==1)
			sc=" (*)";
		else if(changeable==2)
			sc=" (**)";
		size_t i;
		for(i=0;i<value->ranges_number;++i) {
			if(value->ranges[i])
				myprintf(cs,"  %s: %s%s\n",name,value->ranges[i],sc);
		}
	}
}

static void toggle_cli_param(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn) {

		int i=0;

		while(tcmds[i].cmd && tcmds[i].data) {
			if(strcmp(tcmds[i].cmd,pn) == 0) {
				*(tcmds[i].data) = !(*(tcmds[i].data));
				cli_print_flag(cs,*(tcmds[i].data),tcmds[i].cmd,0);
				return;
			}
			++i;
		}

		myprintf(cs, "\n");
		myprintf(cs, "  Error: unknown or constant parameter: %s.\n",pn);
		myprintf(cs, "  You can toggle only the following parameters:\n");
		myprintf(cs, "\n");

		i=0;

		while(tcmds[i].cmd && tcmds[i].data) {
			cli_print_flag(cs,*(tcmds[i].data),tcmds[i].cmd,0);
			++i;
		}

		myprintf(cs,"\n");
	}
}

static void change_cli_param(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn) {

		int i=0;

		while(ccmds[i].cmd) {
			if(strstr(pn,ccmds[i].cmd) == pn) {
				pn += strlen(ccmds[i].cmd);
				while(pn[0]==' ') ++pn;
				vint pnv = (vint)atoi(pn);
				if(ccmds[i].data) {
					*(ccmds[i].data) = (vint)pnv;
					cli_print_uint(cs,(unsigned long)(*(ccmds[i].data)),ccmds[i].cmd,2);
				} else if(strcmp(ccmds[i].cmd,"total-quota")==0) {
					turn_params.users_params.users.total_quota = pnv;
					cli_print_uint(cs,(unsigned long)(turn_params.users_params.users.total_quota),ccmds[i].cmd,2);
				} else if(strcmp(ccmds[i].cmd,"user-quota")==0) {
					turn_params.users_params.users.user_quota = pnv;
					cli_print_uint(cs,(unsigned long)(turn_params.users_params.users.user_quota),ccmds[i].cmd,2);
				}
				return;
			}
			++i;
		}

		myprintf(cs, "\n");
		myprintf(cs, "  Error: unknown or constant parameter: %s.\n",pn);
		myprintf(cs, "  You can change only the following parameters:\n");
		myprintf(cs, "\n");

		i=0;

		while(ccmds[i].cmd) {
			myprintf(cs,"  %s\n",ccmds[i].cmd);
			++i;
		}

		myprintf(cs,"\n");
	}
}

struct ps_arg {
	struct cli_session* cs;
	size_t counter;
	turn_time_t ct;
	const char *username;
	const char *pname;
	int exact_match;
	ur_string_map* users;
	size_t *user_counters;
	char **user_names;
	size_t users_number;
};

static const char* pname(SOCKET_TYPE st)
{
	switch(st) {
	case TCP_SOCKET:
		return "TCP";
	case UDP_SOCKET:
		return "UDP";
	case TLS_SOCKET:
		return "TLS";
	case DTLS_SOCKET:
		return "DTLS";
	case TENTATIVE_TCP_SOCKET:
		return "TCP/TLS";
	default:
		;
	};
	return "UNKNOWN";
}

static int print_session(ur_map_key_type key, ur_map_value_type value, void *arg)
{
	if(key && value && arg) {
		struct ps_arg *csarg = (struct ps_arg*)arg;
		struct cli_session* cs = csarg->cs;
		struct turn_session_info *tsi = (struct turn_session_info *)value;

		if(csarg->users) {
			const char *pn=csarg->pname;
			if(pn[0]) {
				if(!strcmp(pn,"TLS") || !strcmp(pn,"tls") || !strcmp(pn,"Tls")) {
					if(tsi->client_protocol != TLS_SOCKET)
						return 0;
				} else if(!strcmp(pn,"DTLS") || !strcmp(pn,"dtls") || !strcmp(pn,"Dtls")) {
					if(tsi->client_protocol != DTLS_SOCKET)
						return 0;
				} else if(!strcmp(pn,"TCP") || !strcmp(pn,"tcp") || !strcmp(pn,"Tcp")) {
					if(tsi->client_protocol != TCP_SOCKET)
						return 0;
				} else if(!strcmp(pn,"UDP") || !strcmp(pn,"udp") || !strcmp(pn,"Udp")) {
					if(tsi->client_protocol != UDP_SOCKET)
						return 0;
				} else {
					return 0;
				}
			}
			ur_string_map_value_type value;
			if(!ur_string_map_get(csarg->users, (ur_string_map_key_type)(char*)tsi->username, &value)) {
				value = (ur_string_map_value_type)csarg->users_number;
				csarg->users_number += 1;
				csarg->user_counters = (size_t*)turn_realloc(csarg->user_counters,
						(size_t)value * sizeof(size_t),
						csarg->users_number * sizeof(size_t));
				csarg->user_names = (char**)turn_realloc(csarg->user_names,
						(size_t)value * sizeof(char*),
						csarg->users_number * sizeof(char*));
				csarg->user_names[(size_t)value] = turn_strdup((char*)tsi->username);
				csarg->user_counters[(size_t)value] = 0;
				ur_string_map_put(csarg->users, (ur_string_map_key_type)(char*)tsi->username, value);
			}
			csarg->user_counters[(size_t)value] += 1;
		} else {
			if(csarg->username[0]) {
				if(csarg->exact_match) {
					if(strcmp((char*)tsi->username, csarg->username))
						return 0;
				} else {
					if(!strstr((char*)tsi->username, csarg->username))
						return 0;
				}
			}
			if(cs->f || (unsigned long)csarg->counter<(unsigned long)cli_max_output_sessions) {
				myprintf(cs, "\n");
				myprintf(cs,"    %lu) id=%018llu, user <%s>:\n",
								(unsigned long)(csarg->counter+1),
								(unsigned long long)tsi->id,
								tsi->username);
				if(turn_time_before(csarg->ct, tsi->start_time)) {
					myprintf(cs,"      started: undefined time\n");
				} else {
					myprintf(cs,"      started %lu secs ago\n",(unsigned long)(csarg->ct - tsi->start_time));
				}
				if(turn_time_before(tsi->expiration_time,csarg->ct)) {
					myprintf(cs,"      expired\n");
				} else {
					myprintf(cs,"      expiring in %lu secs\n",(unsigned long)(tsi->expiration_time - csarg->ct));
				}
				myprintf(cs,"      client protocol %s, relay protocol %s\n",pname(tsi->client_protocol),pname(tsi->peer_protocol));
				{
					if(!tsi->local_addr_data.saddr[0])
						addr_to_string(&(tsi->local_addr_data.addr),(u08bits*)tsi->local_addr_data.saddr);
					if(!tsi->remote_addr_data.saddr[0])
						addr_to_string(&(tsi->remote_addr_data.addr),(u08bits*)tsi->remote_addr_data.saddr);
					if(!tsi->relay_addr_data.saddr[0])
						addr_to_string(&(tsi->relay_addr_data.addr),(u08bits*)tsi->relay_addr_data.saddr);
					myprintf(cs,"      client addr %s, server addr %s\n",
									tsi->remote_addr_data.saddr,
									tsi->local_addr_data.saddr);
					myprintf(cs,"      relay addr %s\n",
									tsi->relay_addr_data.saddr);
				}
				myprintf(cs,"      fingerprints enforced: %s\n",get_flag(tsi->enforce_fingerprints));
				myprintf(cs,"      mobile: %s\n",get_flag(tsi->is_mobile));
				if(tsi->tls_method[0]) {
					myprintf(cs,"      TLS method: %s\n",tsi->tls_method);
					myprintf(cs,"      TLS cipher: %s\n",tsi->tls_cipher);
				}
				myprintf(cs,"      usage: rp=%lu, rb=%lu, sp=%lu, sb=%lu\n",(unsigned long)(tsi->received_packets), (unsigned long)(tsi->received_bytes),(unsigned long)(tsi->sent_packets),(unsigned long)(tsi->sent_bytes));
				myprintf(cs,"       rate: r=%lu, s=%lu, total=%lu (bytes per sec)\n",(unsigned long)(tsi->received_rate), (unsigned long)(tsi->sent_rate),(unsigned long)(tsi->total_rate));
				if(tsi->main_peers_size) {
					myprintf(cs,"      peers:\n");
					size_t i;
					for(i=0;i<tsi->main_peers_size;++i) {
						if(!(tsi->main_peers_data[i].saddr[0]))
							addr_to_string(&(tsi->main_peers_data[i].addr),(u08bits*)tsi->main_peers_data[i].saddr);
						myprintf(cs,"          %s\n",tsi->main_peers_data[i].saddr);
					}
					if(tsi->extra_peers_size && tsi->extra_peers_data) {
						for(i=0;i<tsi->extra_peers_size;++i) {
							if(!(tsi->extra_peers_data[i].saddr[0]))
								addr_to_string(&(tsi->extra_peers_data[i].addr),(u08bits*)tsi->extra_peers_data[i].saddr);
							myprintf(cs,"          %s\n",tsi->extra_peers_data[i].saddr);
						}
					}
				}
			}
		}

		csarg->counter += 1;
	}
	return 0;
}

static void cancel_session(struct cli_session* cs, const char* ssid)
{
	if(cs && cs->ts && ssid && *ssid) {
		turnsession_id sid = strtoull(ssid,NULL,10);
		send_session_cancellation_to_relay(sid);
	}
}

static void print_sessions(struct cli_session* cs, const char* pn, int exact_match, int print_users)
{
	if(cs && cs->ts && pn) {

		while(pn[0] == ' ') ++pn;
		if(pn[0] == '*') ++pn;

		const char *uname="";
		if(!print_users) {
			uname = pn;
			pn = "";
		}

		struct ps_arg arg = {cs,0,0,uname,pn,exact_match,NULL,NULL,NULL,0};

		arg.ct = turn_time();

		if(print_users) {
			arg.users = ur_string_map_create(NULL);
		}

		ur_map_foreach_arg(cliserver.sessions, (foreachcb_arg_type)print_session, &arg);

		myprintf(cs,"\n");

		if(!print_users && !(cs->f)) {
			if((unsigned long)arg.counter > (unsigned long)cli_max_output_sessions) {
				myprintf(cs,"...\n");
				myprintf(cs,"\n");
			}
		} else if(arg.user_counters && arg.user_names) {
			size_t i;
			for(i=0;i<arg.users_number;++i) {
				if(arg.user_names[i]) {
					myprintf(cs,"    user: <%s>, %lu sessions\n",
						arg.user_names[i],
						(unsigned long)arg.user_counters[i]);
				}
			}
			myprintf(cs,"\n");
		}

		myprintf(cs,"  Total sessions: %lu\n", (unsigned long)arg.counter);
		myprintf(cs,"\n");

		if(!print_users && !(cs->f)) {
			if((unsigned long)arg.counter > (unsigned long)cli_max_output_sessions) {
				myprintf(cs,"  Warning: too many output sessions, more than the\n");
				myprintf(cs,"  current value of cli-max-output-sessions CLI parameter.\n");
				myprintf(cs,"  Refine your request or increase cli-max-output-sessions value.\n");
				myprintf(cs,"\n");
			}
		}

		if(arg.user_counters)
			turn_free(arg.user_counters,sizeof(size_t)*arg.users_number);
		if(arg.user_names) {
			size_t i;
			for(i=0;i<arg.users_number;++i) {
				if(arg.user_names[i])
					turn_free(arg.user_names[i],strlen(arg.user_names[i])+1);
			}
			turn_free(arg.user_names,sizeof(char*) * arg.users_number);
		}
		if(arg.users)
			ur_string_map_free(&arg.users);
	}
}

static void cli_print_configuration(struct cli_session* cs)
{
	if(cs) {
		myprintf(cs,"\n");

		cli_print_flag(cs,turn_params.verbose,"verbose",0);
		cli_print_flag(cs,turn_params.turn_daemon,"daemon process",0);
		cli_print_flag(cs,turn_params.stale_nonce,"stale-nonce",1);
		cli_print_flag(cs,turn_params.stun_only,"stun-only",1);
		cli_print_flag(cs,turn_params.no_stun,"no-stun",1);
		cli_print_flag(cs,turn_params.secure_stun,"secure-stun",1);
		cli_print_flag(cs,turn_params.do_not_use_config_file,"do-not-use-config-file",0);
		cli_print_flag(cs,turn_params.rfc5780,"RFC5780 support",0);
		cli_print_uint(cs,(unsigned int)turn_params.net_engine_version,"net engine version",0);
		cli_print_str(cs,turn_params.net_engine_version_txt[(int)turn_params.net_engine_version],"net engine",0);
		cli_print_flag(cs,turn_params.fingerprint,"enforce fingerprints",0);
		cli_print_flag(cs,turn_params.mobility,"mobility",1);
		cli_print_flag(cs,turn_params.udp_self_balance,"udp-self-balance",0);
		cli_print_str(cs,turn_params.pidfile,"pidfile",0);
		cli_print_uint(cs,(unsigned long)getuid(),"process user ID",0);
		cli_print_uint(cs,(unsigned long)getgid(),"process group ID",0);

		{
			char wd[1025];
			if(getcwd(wd,sizeof(wd)-1)) {
				cli_print_str(cs,wd,"process dir",0);
			}
		}

		myprintf(cs,"\n");

		if(turn_params.cipher_list[0])
			cli_print_str(cs,turn_params.cipher_list,"cipher-list",0);
		else
			cli_print_str(cs,DEFAULT_CIPHER_LIST,"cipher-list",0);

		cli_print_str(cs,turn_params.ec_curve_name,"ec-curve-name",0);
		{
			if(turn_params.dh_key_size == DH_CUSTOM)
				cli_print_str(cs,turn_params.dh_file,"dh-file",0);
			else {
				unsigned int dh_key_length = 1066;
				if(turn_params.dh_key_size == DH_566)
					dh_key_length = 566;
				else if(turn_params.dh_key_size == DH_2066)
					dh_key_length = 2066;
				cli_print_uint(cs,(unsigned long)dh_key_length,"DH-key-length",0);
			}
		}

		cli_print_str(cs,turn_params.ca_cert_file,"Certificate Authority file",0);
		cli_print_str(cs,turn_params.cert_file,"Certificate file",0);
		cli_print_str(cs,turn_params.pkey_file,"Private Key file",0);

		if(turn_params.shatype == SHATYPE_SHA256)
					cli_print_str(cs,"SHA256","SHA type",0);
				else
					cli_print_str(cs,"SHA1","SHA type",0);
		myprintf(cs,"\n");

		cli_print_str_array(cs,turn_params.listener.addrs,turn_params.listener.addrs_number,"Listener addr",0);

		if(turn_params.listener_ifname[0])
			cli_print_str(cs,turn_params.listener_ifname,"listener-ifname",0);

		cli_print_flag(cs,turn_params.no_udp,"no-udp",0);
		cli_print_flag(cs,turn_params.no_tcp,"no-tcp",0);
		cli_print_flag(cs,turn_params.no_dtls,"no-dtls",0);
		cli_print_flag(cs,turn_params.no_tls,"no-tls",0);

		cli_print_flag(cs,(!turn_params.no_sslv3 && !turn_params.no_tls),"SSLv3",0);
		cli_print_flag(cs,(!turn_params.no_tlsv1 && !turn_params.no_tls),"TLSv1.0",0);
		cli_print_flag(cs,(!turn_params.no_tlsv1_1 && !turn_params.no_tls),"TLSv1.1",0);
		cli_print_flag(cs,(!turn_params.no_tlsv1_2 && !turn_params.no_tls),"TLSv1.2",0);

		cli_print_uint(cs,(unsigned long)turn_params.listener_port,"listener-port",0);
		cli_print_uint(cs,(unsigned long)turn_params.tls_listener_port,"tls-listener-port",0);
		cli_print_uint(cs,(unsigned long)turn_params.alt_listener_port,"alt-listener-port",0);
		cli_print_uint(cs,(unsigned long)turn_params.alt_tls_listener_port,"alt-tls-listener-port",0);

		cli_print_addr(cs,turn_params.external_ip,0,"External public IP",0);

		myprintf(cs,"\n");

		cli_print_addr_list(cs,&turn_params.aux_servers_list,1,"Aux server",0);
		cli_print_addr_list(cs,&turn_params.alternate_servers_list,1,"Alternate server",0);
		cli_print_addr_list(cs,&turn_params.tls_alternate_servers_list,1,"TLS alternate server",0);

		myprintf(cs,"\n");

		cli_print_str_array(cs,turn_params.relay_addrs,turn_params.relays_number,"Relay addr",0);

		if(turn_params.relay_ifname[0])
			cli_print_str(cs,turn_params.relay_ifname,"relay-ifname",0);

		cli_print_flag(cs,turn_params.server_relay,"server-relay",0);

		cli_print_flag(cs,turn_params.no_udp_relay,"no-udp-relay",1);
		cli_print_flag(cs,turn_params.no_tcp_relay,"no-tcp-relay",1);

		cli_print_uint(cs,(unsigned long)turn_params.min_port,"min-port",0);
		cli_print_uint(cs,(unsigned long)turn_params.max_port,"max-port",0);

		cli_print_ip_range_list(cs,&turn_params.ip_whitelist,"Whitelist IP",0);
		cli_print_ip_range_list(cs,&turn_params.ip_blacklist,"Blacklist IP",0);

		cli_print_flag(cs,turn_params.no_multicast_peers,"no-multicast-peers",1);
		cli_print_flag(cs,turn_params.no_loopback_peers,"no-loopback-peers",1);

		myprintf(cs,"\n");

		if(turn_params.users_params.userdb[0]) {
			switch(turn_params.users_params.userdb_type) {
			case TURN_USERDB_TYPE_FILE:
				cli_print_str(cs,"file","DB type",0);
				break;
#if !defined(TURN_NO_PQ)
			case TURN_USERDB_TYPE_PQ:
				cli_print_str(cs,"Postgres","DB type",0);
				break;
#endif
#if !defined(TURN_NO_MYSQL)
			case TURN_USERDB_TYPE_MYSQL:
				cli_print_str(cs,"MySQL/MariaDB","DB type",0);
				break;
#endif
#if !defined(TURN_NO_HIREDIS)
			case TURN_USERDB_TYPE_REDIS:
				cli_print_str(cs,"redis","DB type",0);
				break;
#endif
			default:
				cli_print_str(cs,"unknown","DB type",0);
			};
			cli_print_str(cs,turn_params.users_params.userdb,"DB",0);
		} else {
			cli_print_str(cs,"none","DB type",0);
			cli_print_str(cs,"none","DB",0);
		}

#if !defined(TURN_NO_HIREDIS)
		if(turn_params.use_redis_statsdb && turn_params.redis_statsdb[0])
			cli_print_str(cs,turn_params.redis_statsdb,"Redis Statistics DB",0);
#endif

		myprintf(cs,"\n");

		cli_print_flag(cs,turn_params.users_params.use_lt_credentials,"Long-term authorization mechanism",0);
		cli_print_flag(cs,turn_params.users_params.use_st_credentials,"Short-term authorization mechanism",0);
		cli_print_flag(cs,turn_params.users_params.anon_credentials,"Anonymous credentials",0);
		cli_print_flag(cs,turn_params.users_params.use_auth_secret_with_timestamp,"REST API",0);
		if(turn_params.users_params.use_auth_secret_with_timestamp && turn_params.users_params.rest_api_separator)
			cli_print_uint(cs,turn_params.users_params.rest_api_separator,"REST API separator ASCII number",0);

		if(turn_params.users_params.global_realm[0])
			cli_print_str(cs,turn_params.users_params.global_realm,"Realm",0);

		myprintf(cs,"\n");

		cli_print_uint(cs,(unsigned long)turn_params.users_params.users.total_quota,"total-quota",2);
		cli_print_uint(cs,(unsigned long)turn_params.users_params.users.user_quota,"user-quota",2);
		cli_print_uint(cs,(unsigned long)turn_params.users_params.users.total_current_allocs,"total-current-allocs",0);
		cli_print_uint(cs,(unsigned long)turn_params.max_bps,"max-bps",0);

		myprintf(cs,"\n");

		cli_print_uint(cs,(unsigned long)cli_max_output_sessions,"cli-max-output-sessions",2);

		{
		  myprintf(cs,"\n");
		  const char *str="  (Note 1: parameters with (*) are toggleable)";
		  myprintf(cs,"%s\n",str);
		  myprintf(cs,"\n");
		  str="  (Note 2: parameters with (**) are changeable)";
		  myprintf(cs,"%s\n",str);
		  myprintf(cs,"\n");
		}
	}
}

static void close_cli_session(struct cli_session* cs);

static int run_cli_output(struct cli_session* cs, const char *buf, unsigned int len)
{
	if(cs && buf && len) {
		if(bufferevent_write(cs->bev, buf, len)< 0) {
			return -1;
		}
		return 0;
	}
	return -1;
}

static void close_cli_session(struct cli_session* cs)
{
	if(cs) {

		addr_debug_print(cliserver.verbose, &(cs->addr),"CLI session disconnected from");

		if(cs->ts) {
			telnet_free(cs->ts);
			cs->ts = NULL;
		}

		if(cs->bev) {
			bufferevent_flush(cs->bev,EV_READ|EV_WRITE,BEV_FLUSH);
			bufferevent_disable(cs->bev,EV_READ|EV_WRITE);
			bufferevent_free(cs->bev);
			cs->bev=NULL;
		}

		if(cs->fd>=0) {
			close(cs->fd);
			cs->fd = -1;
		}

		turn_free(cs,sizeof(struct cli_session));
	}
}

static void type_cli_cursor(struct cli_session* cs)
{
	if(cs && (cs->bev)) {
	  myprintf(cs, "%s", CLI_CURSOR);
	}
}

static void cli_add_alternate_server(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn && *pn) {
		add_alternate_server(pn);
	}
}

static void cli_add_tls_alternate_server(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn && *pn) {
		add_tls_alternate_server(pn);
	}
}

static void cli_del_alternate_server(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn && *pn) {
		del_alternate_server(pn);
	}
}

static void cli_del_tls_alternate_server(struct cli_session* cs, const char* pn)
{
	if(cs && cs->ts && pn && *pn) {
		del_tls_alternate_server(pn);
	}
}

static int run_cli_input(struct cli_session* cs, const char *buf0, unsigned int len)
{
	int ret = 0;

	if(cs && buf0 && cs->ts && cs->bev) {

		char *buf = (char*)turn_malloc(len+1);
		ns_bcopy(buf0,buf,len);
		buf[len]=0;

		char *cmd = buf;

		while((cmd[0]==' ') || (cmd[0]=='\t')) ++cmd;

		size_t sl = strlen(cmd);

		while(sl) {
			char c = cmd[sl-1];
			if((c==10)||(c==13)) {
				cmd[sl-1]=0;
				--sl;
			} else {
				break;
			}
		}

		if(sl) {
			cs->cmds += 1;
			if(cli_password[0] && !(cs->auth_completed)) {
				if(strcmp(cmd,cli_password)) {
					if(cs->cmds>=CLI_PASSWORD_TRY_NUMBER) {
						addr_debug_print(1, &(cs->addr),"CLI authentication error");
						TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR,"CLI authentication error\n");
						close_cli_session(cs);
					} else {
						const char* ipwd="Enter password: ";
						myprintf(cs,"%s\n",ipwd);
					}
				} else {
					cs->auth_completed = 1;
					addr_debug_print(1, &(cs->addr),"CLI authentication success");
					type_cli_cursor(cs);
				}
			} else if((strcmp(cmd,"bye") == 0)||(strcmp(cmd,"quit") == 0)||(strcmp(cmd,"exit") == 0)||(strcmp(cmd,"q") == 0)) {
				const char* str="Bye !";
				myprintf(cs,"%s\n",str);
				close_cli_session(cs);
				ret = -1;
			} else if((strcmp(cmd,"halt") == 0)||(strcmp(cmd,"shutdown") == 0)||(strcmp(cmd,"stop") == 0)) {
				addr_debug_print(1, &(cs->addr),"Shutdown command received from CLI user");
				const char* str="TURN server is shutting down";
				myprintf(cs,"%s\n",str);
				close_cli_session(cs);
				turn_params.stop_turn_server = 1;
				sleep(10);
				exit(0);
			} else if((strcmp(cmd,"?") == 0)||(strcmp(cmd,"h") == 0)||(strcmp(cmd,"help") == 0)) {
				print_str_array(cs, CLI_GREETING_STR);
				print_str_array(cs, CLI_HELP_STR);
				type_cli_cursor(cs);
			} else if(strcmp(cmd,"pc")==0) {
				cli_print_configuration(cs);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"tc ") == cmd) {
				toggle_cli_param(cs,cmd+3);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"tc") == cmd) {
				toggle_cli_param(cs,cmd+2);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"psp") == cmd) {
				print_sessions(cs,cmd+3,0,0);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"psd") == cmd) {
				cmd += 3;
				while(cmd[0]==' ') ++cmd;
				if(!(cmd[0])) {
					const char* str="You have to provide file name for ps dump\n";
					myprintf(cs,"%s\n",str);
				} else {
					cs->f = fopen(cmd,"w");
					if(!(cs->f)) {
						const char* str="Cannot open file for writing\n";
						myprintf(cs,"%s\n",str);
					} else {
						print_sessions(cs,"",1,0);
						fclose(cs->f);
						cs->f = NULL;
					}
				}
				type_cli_cursor(cs);
			} else if(strstr(cmd,"pu ") == cmd) {
				print_sessions(cs,cmd+3,0,1);
				type_cli_cursor(cs);
			} else if(!strcmp(cmd,"pu")) {
				print_sessions(cs,cmd+2,0,1);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"ps") == cmd) {
				print_sessions(cs,cmd+2,1,0);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"cs ") == cmd) {
				cancel_session(cs,cmd+3);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"lr") == cmd) {
				log_reset(cs);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"cc ") == cmd) {
				change_cli_param(cs,cmd+3);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"cc") == cmd) {
				change_cli_param(cs,cmd+2);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"aas ") == cmd) {
				cli_add_alternate_server(cs,cmd+4);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"atas ") == cmd) {
				cli_add_tls_alternate_server(cs,cmd+5);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"das ") == cmd) {
				cli_del_alternate_server(cs,cmd+4);
				type_cli_cursor(cs);
			} else if(strstr(cmd,"dtas ") == cmd) {
				cli_del_tls_alternate_server(cs,cmd+5);
				type_cli_cursor(cs);
			} else {
				const char* str="Unknown command\n";
				myprintf(cs,"%s\n",str);
				type_cli_cursor(cs);
			}
		} else {
			type_cli_cursor(cs);
		}

		turn_free(buf,len+1);
	}

	return ret;
}

static void cli_socket_input_handler_bev(struct bufferevent *bev, void* arg)
{
	if (bev && arg) {

		struct cli_session* cs = (struct cli_session*) arg;

		if(!(cs->ts))
			return;

		stun_buffer buf;

		if(cs->bev) {

			int len = (int)bufferevent_read(cs->bev, buf.buf, STUN_BUFFER_SIZE-1);
			if(len < 0) {
				close_cli_session(cs);
				return;
			} else if(len == 0) {
				return;
			}

			buf.len = len;
			buf.offset = 0;
			buf.buf[len]=0;

			telnet_recv(cs->ts, (const char *)buf.buf, (unsigned int)(buf.len));
		}
	}
}

static void cli_eventcb_bev(struct bufferevent *bev, short events, void *arg)
{
	UNUSED_ARG(bev);

	if (events & BEV_EVENT_CONNECTED) {
		// Connect okay
	} else if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
		if (arg) {

			struct cli_session* cs = (struct cli_session*) arg;

			close_cli_session(cs);
		}
	}
}

static void cli_telnet_event_handler(telnet_t *telnet, telnet_event_t *event, void *user_data)
{
	if (user_data && telnet) {

		struct cli_session *cs = (struct cli_session *) user_data;

		switch (event->type){
		case TELNET_EV_DATA:
			run_cli_input(cs, event->data.buffer, event->data.size);
			break;
		case TELNET_EV_SEND:
			run_cli_output(cs, event->data.buffer, event->data.size);
			break;
		case TELNET_EV_ERROR:
			TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "TELNET error: %s", event->error.msg);
			break;
		default:
			;
		};
	}
}

static void cliserver_input_handler(struct evconnlistener *l, evutil_socket_t fd,
				struct sockaddr *sa, int socklen, void *arg)
{
	UNUSED_ARG(l);
	UNUSED_ARG(arg);
	UNUSED_ARG(socklen);

	addr_debug_print(cliserver.verbose, (ioa_addr*)sa,"CLI connected to");

	struct cli_session *clisession = (struct cli_session*)turn_malloc(sizeof(struct cli_session));
	ns_bzero(clisession,sizeof(struct cli_session));

	set_socket_options_fd(fd, 1, sa->sa_family);

	clisession->fd = fd;

	addr_cpy(&(clisession->addr),(ioa_addr*)sa);

	clisession->bev = bufferevent_socket_new(cliserver.event_base,
					fd,
					TURN_BUFFEREVENTS_OPTIONS);
	bufferevent_setcb(clisession->bev, cli_socket_input_handler_bev, NULL,
			cli_eventcb_bev, clisession);
	bufferevent_setwatermark(clisession->bev, EV_READ|EV_WRITE, 0, BUFFEREVENT_HIGH_WATERMARK);
	bufferevent_enable(clisession->bev, EV_READ); /* Start reading. */

	clisession->ts = telnet_init(cli_telopts, cli_telnet_event_handler, 0, clisession);

	if(!(clisession->ts)) {
		const char *str = "Cannot open telnet session\n";
		addr_debug_print(cliserver.verbose, (ioa_addr*)sa,str);
		close_cli_session(clisession);
	} else {
	  print_str_array(clisession, CLI_GREETING_STR);
	  telnet_printf(clisession->ts,"\n");
	  telnet_printf(clisession->ts,"Type '?' for help\n");
	  if(cli_password[0]) {
	    const char* ipwd="Enter password: ";
	    telnet_printf(clisession->ts,"%s\n",ipwd);
	  } else {
	    type_cli_cursor(clisession);
	  }
	}
}

void setup_cli_thread(void)
{
	cliserver.event_base = turn_event_base_new();
	TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO,"IO method (cli thread): %s\n",event_base_get_method(cliserver.event_base));

	struct bufferevent *pair[2];

	bufferevent_pair_new(cliserver.event_base, TURN_BUFFEREVENTS_OPTIONS, pair);
	cliserver.in_buf = pair[0];
	cliserver.out_buf = pair[1];
	bufferevent_setcb(cliserver.in_buf, cli_server_receive_message, NULL, NULL, &cliserver);
	bufferevent_enable(cliserver.in_buf, EV_READ);

	if(!cli_addr_set) {
		if(make_ioa_addr((const u08bits*)CLI_DEFAULT_IP,0,&cli_addr)<0) {
			TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR,"Cannot set cli address %s\n",CLI_DEFAULT_IP);
			return;
		}
	}

	addr_set_port(&cli_addr,cli_port);

	cliserver.listen_fd = socket(cli_addr.ss.sa_family, SOCK_STREAM, 0);
	if (cliserver.listen_fd < 0) {
	    perror("socket");
	    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR,"Cannot open CLI socket\n");
	    return;
	}

	if(addr_bind(cliserver.listen_fd,&cli_addr,1)<0) {
	  perror("Cannot bind CLI socket to addr");
	  char saddr[129];
	  addr_to_string(&cli_addr,(u08bits*)saddr);
	  TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR,"Cannot bind CLI listener socket to addr %s\n",saddr);
	  socket_closesocket(cliserver.listen_fd);
	  return;
	}

	socket_tcp_set_keepalive(cliserver.listen_fd);

	socket_set_nonblocking(cliserver.listen_fd);

	cliserver.l = evconnlistener_new(cliserver.event_base,
			  cliserver_input_handler, &cliserver,
			  LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
			  1024, cliserver.listen_fd);

	if(!(cliserver.l)) {
	  TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR,"Cannot create CLI listener\n");
	  socket_closesocket(cliserver.listen_fd);
	  return;
	}

	cliserver.sessions = ur_map_create();

	addr_debug_print(cliserver.verbose, &cli_addr,"CLI listener opened on ");
}

void cli_server_receive_message(struct bufferevent *bev, void *ptr)
{
	UNUSED_ARG(ptr);

	struct turn_session_info *tsi = (struct turn_session_info*)turn_malloc(sizeof(struct turn_session_info));
	turn_session_info_init(tsi);
	int n = 0;
	struct evbuffer *input = bufferevent_get_input(bev);

	while ((n = evbuffer_remove(input, tsi, sizeof(struct turn_session_info))) > 0) {
		if (n != sizeof(struct turn_session_info)) {
			fprintf(stderr,"%s: Weird CLI buffer error: size=%d\n",__FUNCTION__,n);
			continue;
		}

		ur_map_value_type t = 0;
		if (ur_map_get(cliserver.sessions, (ur_map_key_type)tsi->id, &t) && t) {
			struct turn_session_info *old = (struct turn_session_info*)t;
			turn_session_info_clean(old);
			turn_free(old,sizeof(struct turn_session_info));
			ur_map_del(cliserver.sessions, (ur_map_key_type)tsi->id, NULL);
		}

		if(tsi->valid) {
			ur_map_put(cliserver.sessions, (ur_map_key_type)tsi->id, (ur_map_value_type)tsi);
			tsi = (struct turn_session_info*)turn_malloc(sizeof(struct turn_session_info));
			turn_session_info_init(tsi);
		} else {
			turn_session_info_clean(tsi);
		}
	}

	if(tsi) {
		turn_session_info_clean(tsi);
		turn_free(tsi,sizeof(struct turn_session_info));
	}
}

int send_turn_session_info(struct turn_session_info* tsi)
{
	int ret = -1;

	if(!use_cli)
		return ret;

	if(tsi) {
		struct evbuffer *output = bufferevent_get_output(cliserver.out_buf);
		if(output) {
			if(evbuffer_add(output,tsi,sizeof(struct turn_session_info))>=0) {
				ret = 0;
			}
		}
	}

	return ret;
}

///////////////////////////////
