/*
 * radiusd.c	Main loop of the radius server.
 *
 * Version:	$Id$
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2000,2001,2002,2003,2004  The FreeRADIUS server project
 * Copyright 1999,2000  Miquel van Smoorenburg <miquels@cistron.nl>
 * Copyright 2000  Alan DeKok <aland@ox.org>
 * Copyright 2000  Alan Curry <pacman-radius@cqc.com>
 * Copyright 2000  Jeff Carneal <jeff@apex.net>
 * Copyright 2000  Chad Miller <cmiller@surfsouth.com>
 */

/* don't look here for the version, run radiusd -v or look in version.c */
static const char rcsid[] =
"$Id$";

#include "autoconf.h"

#include <sys/file.h>

#ifdef HAVE_NETINET_IN_H
#	include <netinet/in.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <signal.h>

#ifdef HAVE_GETOPT_H
#	include <getopt.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#	include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#	include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
#	define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#	define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include "radiusd.h"
#include "rad_assert.h"
#include "conffile.h"
#include "modules.h"
#include "request_list.h"
#include "radius_snmp.h"

/*
 *  Global variables.
 */
const char *progname = NULL;
const char *radius_dir = NULL;
const char *radacct_dir = NULL;
const char *radlog_dir = NULL;
const char *radlib_dir = NULL;
int log_stripped_names;
int debug_flag = 0;
int log_auth_detail = FALSE;
int need_reload = FALSE;
const char *radiusd_version = "FreeRADIUS Version " RADIUSD_VERSION ", for host " HOSTINFO ", built on " __DATE__ " at " __TIME__;

static time_t time_now;
static pid_t radius_pid;

/*
 *  Configuration items.
 */
static time_t start_time = 0;
static int do_exit = 0;

/*
 *	Static functions.
 */
static void usage(int);

static void sig_fatal (int);
static void sig_hup (int);

static int rad_status_server(REQUEST *request);


/*
 *	Check if an incoming request is "ok"
 *
 *	It takes packets, not requests.  It sees if the packet looks
 *	OK.  If so, it does a number of sanity checks on it.
  */
static RAD_REQUEST_FUNP packet_ok(RADIUS_PACKET *packet,
				  rad_listen_t *listener)
{
	REQUEST		*curreq;
	RAD_REQUEST_FUNP fun = NULL;

	/*
	 *	Some sanity checks, based on the packet code.
	 */
	switch(packet->code) {
		case PW_AUTHENTICATION_REQUEST:
			/*
			 *	Check for requests sent to the wrong
			 *	port, and ignore them, if so.
			 */
			if (listener->type != RAD_LISTEN_AUTH) {
				RAD_SNMP_INC(rad_snmp.auth.total_packets_dropped);
				radlog(L_ERR, "Authentication-Request sent to a non-authentication port from "
				       "client %s port %d - ID %d : IGNORED",
				       client_name(&packet->src_ipaddr),
				       packet->src_port, packet->id);
				return NULL;
			}
			fun = rad_authenticate;
			break;

		case PW_ACCOUNTING_REQUEST:
			/*
			 *	Check for requests sent to the wrong
			 *	port, and ignore them, if so.
			 */
			if (listener->type != RAD_LISTEN_ACCT) {
				RAD_SNMP_INC(rad_snmp.acct.total_packets_dropped);
				radlog(L_ERR, "Accounting-Request packet sent to a non-accounting port from "
				       "client %s port %d - ID %d : IGNORED",
				       client_name(&packet->src_ipaddr),
				       packet->src_port, packet->id);
				return NULL;
			}
			fun = rad_accounting;
			break;

		case PW_AUTHENTICATION_ACK:
		case PW_ACCESS_CHALLENGE:
		case PW_AUTHENTICATION_REJECT:
			/*
			 *	Replies NOT sent to the proxy port get
			 *	an error message logged, and the
			 *	packet is dropped.
			 */
			if (listener->type != RAD_LISTEN_PROXY) {
				RAD_SNMP_INC(rad_snmp.auth.total_packets_dropped);
				radlog(L_ERR, "Authentication reply packet code %d sent to a non-proxy reply port from "
				       "client %s port %d - ID %d : IGNORED",
				       packet->code,
				       client_name(&packet->src_ipaddr),
				       packet->src_port, packet->id);
				return NULL;
			}
			fun = rad_authenticate;
			break;

		case PW_ACCOUNTING_RESPONSE:
			/*
			 *	Replies NOT sent to the proxy port get
			 *	an error message logged, and the
			 *	packet is dropped.
			 */
			if (listener->type != RAD_LISTEN_PROXY) {
				RAD_SNMP_INC(rad_snmp.acct.total_packets_dropped);
				radlog(L_ERR, "Accounting reply packet code %d sent to a non-proxy reply port from "
				       "client %s port %d - ID %d : IGNORED",
				       packet->code,
				       client_name(&packet->src_ipaddr),
				       packet->src_port, packet->id);
				return 0;
			}
			fun = rad_accounting;
			break;

		case PW_STATUS_SERVER:
			if (!mainconfig.status_server) {
				DEBUG("WARNING: Ignoring Status-Server request due to security configuration");
				return NULL;
			}
			fun = rad_status_server;
			break;

		case PW_PASSWORD_REQUEST:
			RAD_SNMP_INC(rad_snmp.auth.total_unknown_types);

			/*
			 *  We don't support this anymore.
			 */
			radlog(L_ERR, "Deprecated password change request from client %s port %d - ID %d : IGNORED",
					client_name(&packet->src_ipaddr),
			       packet->src_port, packet->id);
			return NULL;
			break;

		default:
			RAD_SNMP_INC(rad_snmp.auth.total_unknown_types);

			radlog(L_ERR, "Unknown packet code %d from client %s port %d "
			       "- ID %d : IGNORED", packet->code,
			       client_name(&packet->src_ipaddr),
			       packet->src_port, packet->id);
			return NULL;
			break;

	} /* switch over packet types */

	/*
	 *	Don't handle proxy replies here.  They need to
	 *	return the *old* request, so we can re-process it.
	 */
	if (listener->type == RAD_LISTEN_PROXY) {
		return fun;
	}

	/*
	 *	If there is no existing request of id, code, etc.,
	 *	then we can return, and let it be processed.
	 */
	if ((curreq = rl_find(packet)) == NULL) {
		/*
		 *	Count the total number of requests, to see if
		 *	there are too many.  If so, return with an
		 *	error.
		 */
		if (mainconfig.max_requests) {
			int request_count = rl_num_requests();

			/*
			 *	This is a new request.  Let's see if
			 *	it makes us go over our configured
			 *	bounds.
			 */
			if (request_count > mainconfig.max_requests) {
				radlog(L_ERR, "Dropping request (%d is too many): "
				       "from client %s port %d - ID: %d", request_count,
				       client_name(&packet->src_ipaddr),
				       packet->src_port, packet->id);
				radlog(L_INFO, "WARNING: Please check the radiusd.conf file.\n"
				       "\tThe value for 'max_requests' is probably set too low.\n");
				return NULL;
			} /* else there were a small number of requests */
		} /* else there was no configured limit for requests */

		/*
		 *	FIXME: Add checks for system load.  If the
		 *	system is busy, start dropping requests...
		 *
		 *	We can probably keep some statistics
		 *	ourselves...  if there are more requests
		 *	coming in than we can handle, start dropping
		 *	some.
		 */

		return fun;
	}

	/*
	 *	"fake" requests MUST NEVER be in the request list.
	 *
	 *	They're used internally in the server.  Any reply
	 *	is a reply to the local server, and any proxied packet
	 *	gets sent outside of the tunnel.
	 */
	rad_assert((curreq->options & RAD_REQUEST_OPTION_FAKE_REQUEST) == 0);

	/*
	 *	The current request isn't finished, which
	 *	means that the NAS sent us a new packet, while
	 *	we are still processing the old request.
	 */
	if (!curreq->finished) {
		/*
		 *	If the authentication vectors are identical,
		 *	then the NAS is re-transmitting it, trying to
		 *	kick us into responding to the request.
		 */
		if (memcmp(curreq->packet->vector, packet->vector,
			   sizeof(packet->vector)) == 0) {
			RAD_SNMP_INC(rad_snmp.auth.total_dup_requests);

			/*
			 *	It's not finished because the request
			 *	was proxied, but there was no reply
			 *	from the home server.
			 */
			if (curreq->proxy && !curreq->proxy_reply) {
				/*
				 *	We're taking care of sending
				 *	duplicate proxied packets, so
				 *	we ignore any duplicate
				 *	requests from the NAS.
				 *
				 *	FIXME: Make it ALWAYS synchronous!
				 */
				if (!mainconfig.proxy_synchronous) {
					RAD_SNMP_TYPE_INC(listener, total_packets_dropped);

					DEBUG2("Ignoring duplicate packet from client "
					       "%s port %d - ID: %d, due to outstanding proxied request %d.",
					       client_name(&packet->src_ipaddr),
					       packet->src_port, packet->id,
					       curreq->number);
					return NULL;

					/*
					 *	We ARE proxying the request,
					 *	and we have NOT received a
					 *	proxy reply yet, and we ARE
					 *	doing synchronous proxying.
					 *
					 *	In that case, go kick
					 *	the home RADIUS server
					 *	again.
					 */
				} else {
					char buffer[128];

					DEBUG2("Sending duplicate proxied request to home server %s port %d - ID: %d",
					       inet_ntop(curreq->proxy->dst_ipaddr.af,
							 &curreq->proxy->dst_ipaddr.ipaddr,
							 buffer, sizeof(buffer)),					       curreq->proxy->dst_port,

					       curreq->proxy->id);
				}
				curreq->proxy_next_try = time_now + mainconfig.proxy_retry_delay;
				rad_send(curreq->proxy, curreq->packet,
					 curreq->proxysecret);
				return NULL;
			} /* else the packet was not proxied */

			/*
			 *	Someone's still working on it, so we
			 *	ignore the duplicate request.
			 */
			radlog(L_ERR, "Discarding duplicate request from "
			       "client %s port %d - ID: %d due to unfinished request %d",
			       client_name(&packet->src_ipaddr),
			       packet->src_port, packet->id,
			       curreq->number);
			return NULL;
		} /* else the authentication vectors were different */

		/*
		 *	The authentication vectors are different, so
		 *	the NAS has given up on us, as we've taken too
		 *	long to process the request.  This is a
		 *	SERIOUS problem!
		 */
		RAD_SNMP_TYPE_INC(listener, total_packets_dropped);

		radlog(L_ERR, "Dropping conflicting packet from "
		       "client %s port %d - ID: %d due to unfinished request %d",
		       client_name(&packet->src_ipaddr),
		       packet->src_port, packet->id,
		       curreq->number);
		return NULL;
	}

	/*
	 *	The old request is finished.  We now check the
	 *	authentication vectors.  If the client has sent us a
	 *	request with identical code && ID, but different
	 *	vector, then they MUST have gotten our response, so we
	 *	can delete the original request, and process the new
	 *	one.
	 *
	 *	If the vectors are the same, then it's a duplicate
	 *	request, and we can send a duplicate reply.
	 */
	if (memcmp(curreq->packet->vector, packet->vector,
		   sizeof(packet->vector)) == 0) {
		RAD_SNMP_INC(rad_snmp.auth.total_dup_requests);

		/*
		 *	If the packet has been delayed, then silently
		 *	send a response, and clear the delayed flag.
		 *
		 *	Note that this means if the NAS kicks us while
		 *	we're delaying a reject, then the reject may
		 *	be sent sooner than otherwise.
		 *
		 *	This COULD be construed as a bug.  Maybe what
		 *	we want to do is to ignore the duplicate
		 *	packet, and send the reject later.
		 */
		if (curreq->options & RAD_REQUEST_OPTION_DELAYED_REJECT) {
			curreq->options &= ~RAD_REQUEST_OPTION_DELAYED_REJECT;
			rad_send(curreq->reply, curreq->packet, curreq->secret);
			return NULL;
		}

		/*
		 *	Maybe we've saved a reply packet.  If so,
		 *	re-send it.  Otherwise, just complain.
		 */
		if (curreq->reply->code != 0) {
			DEBUG2("Sending duplicate reply "
			       "to client %s port %d - ID: %d",
			       client_name(&packet->src_ipaddr),
			       packet->src_port, packet->id);
			rad_send(curreq->reply, curreq->packet, curreq->secret);
			return NULL;
		}

		/*
		 *	Else we never sent a reply to the NAS,
		 *	as we decided somehow we didn't like the request.
		 *
		 *	This shouldn't happen, in general...
		 */
		DEBUG2("Discarding duplicate request from client %s port %d - ID: %d",
		       client_name(&packet->src_ipaddr),
		       packet->src_port, packet->id);
		return NULL;
	} /* else the vectors were different, so we discard the old request. */

	/*
	 *	'packet' has the same source IP, source port, code,
	 *	and Id as 'curreq', but a different authentication
	 *	vector.  We can therefore delete 'curreq', as we were
	 *	only keeping it around to send out duplicate replies,
	 *	if the first reply got lost in the network.
	 */
	rl_delete(curreq);

	/*
	 *	The request is OK.  We can process it...
	 *
	 *	Don't bother checking the maximum nubmer of requests
	 *	here.  we've just deleted one, so we KNOW we're under
	 *	the limit if we add one more.
	 */
	return fun;
}


/*
 *  Do a proxy check of the REQUEST list when using the new proxy code.
 */
static REQUEST *proxy_ok(RADIUS_PACKET *packet)
{
	REALM *cl;
	REQUEST *oldreq;
	char buffer[128];

	/*
	 *	Find the original request in the request list
	 */
	oldreq = rl_find_proxy(packet);

	/*
	 *	If we haven't found the original request which was
	 *	sent, to get this reply.  Complain, and discard this
	 *	request, as there's no way for us to send it to a NAS.
	 */
	if (!oldreq) {
		radlog(L_PROXY, "No outstanding request was found for proxy reply from home server %s port %d - ID %d",
		       inet_ntop(packet->src_ipaddr.af,
				 &packet->src_ipaddr.ipaddr,
				 buffer, sizeof(buffer)),
		       packet->src_port, packet->id);
		return NULL;
	}

	/*
	 *	The proxy reply has arrived too late, as the original
	 *	(old) request has timed out, been rejected, and marked
	 *	as finished.  The client has already received a
	 *	response, so there is nothing that can be done. Delete
	 *	the tardy reply from the home server, and return NULL.
	 */
	if ((oldreq->reply->code != 0) ||
	    (oldreq->finished)) {
		radlog(L_ERR, "Reply from home server %s port %d  - ID: %d arrived too late for request %d. Try increasing 'retry_delay' or 'max_request_time'",
		       inet_ntop(packet->src_ipaddr.af,
				 &packet->src_ipaddr.ipaddr,
				 buffer, sizeof(buffer)),
		       packet->src_port, packet->id,
		       oldreq->number);
		return NULL;
	}

	/*
	 *	If there is already a reply, maybe this one is a
	 *	duplicate?
	 */
	if (oldreq->proxy_reply) {
		if (memcmp(oldreq->proxy_reply->vector,
			   packet->vector,
			   sizeof(oldreq->proxy_reply->vector)) == 0) {
			radlog(L_ERR, "Discarding duplicate reply from home server %s port %d  - ID: %d for request %d",
			       inet_ntop(packet->src_ipaddr.af,
					 &packet->src_ipaddr.ipaddr,
					 buffer, sizeof(buffer)),
			       packet->src_port, packet->id,
			       oldreq->number);
		} else {
			/*
			 *	? The home server gave us a new *
			 *	proxy reply, which doesn't match * the
			 *	old one.  Delete it
			 !  */
			DEBUG2("Ignoring conflicting proxy reply");
		}

		/*
		 *	We've already received a reply, so
		 *	we discard this one, as we don't want
		 *	to do duplicate work.
		 */
		return NULL;
	} /* else there wasn't a proxy reply yet, so we can process it */

	/*
	 *	 Refresh the old request, and update it with the proxy
	 *	 reply.
	 *
	 *	? Can we delete the proxy request here?  * Is there
	 *	any more need for it?
	 *
	 *	FIXME: we probably shouldn't be updating the time
	 *	stamp here.
	 */
	oldreq->timestamp = time_now;
	oldreq->proxy_reply = packet;

	/*
	 *	Now that we've verified the packet IS actually
	 *	from that realm, and not forged, we can go mark the
	 *	realms for this home server as active.
	 *
	 *	If we had done this check in the 'find realm by IP address'
	 *	function, then an attacker could force us to use a home
	 *	server which was inactive, by forging reply packets
	 *	which didn't match any request.  We would think that
	 *	the reply meant the home server was active, would
	 *	re-activate the realms, and THEN bounce the packet
	 *	as garbage.
	 */
	for (cl = mainconfig.realms; cl != NULL; cl = cl->next) {
		if (oldreq->proxy_reply->src_ipaddr.af != cl->ipaddr.af) continue;
		if (cl->ipaddr.af != AF_INET) continue; /* FIXME */

		if (oldreq->proxy_reply->src_ipaddr.ipaddr.ip4addr.s_addr == cl->ipaddr.ipaddr.ip4addr.s_addr) {
			if (oldreq->proxy_reply->src_port == cl->auth_port) {
				cl->active = TRUE;
				cl->last_reply = oldreq->timestamp;
			} else if (oldreq->proxy_reply->src_port == cl->acct_port) {
				cl->acct_active = TRUE;
				cl->last_reply = oldreq->timestamp;
			}
		}
	}

	return oldreq;
}

/*
 *	Do more checks, this time on the REQUEST data structure.
 *
 *	The main purpose of this code is to handle proxied requests.
 */
static REQUEST *request_ok(RADIUS_PACKET *packet, uint8_t *secret,
			   rad_listen_t *listener)
{
	REQUEST		*request = NULL;

	/*
	 *	If the request has come in on the proxy FD, then
	 *	it's a proxy reply, so pass it through the code which
	 *	tries to find the original request, which we should
	 *	process, rather than processing the reply as a "new"
	 *	request.
	 */
	if (listener->type == RAD_LISTEN_PROXY) {
		/*
		 *	Find the old request, based on the current
		 *	packet.
		 */
		request = proxy_ok(packet);
		if (!request) {
			return NULL;
		}
		rad_assert(request->magic == REQUEST_MAGIC);

		/*
		 *	We must have passed through the code below
		 *	for the original request, which adds the
		 *	reply packet to it.
		 */
		rad_assert(request->reply != NULL);

	} else {		/* remember the new request */
		/*
		 *	A unique per-request counter.
		 */
		static int request_num_counter = 0;

		request = request_alloc(); /* never fails */
		request->packet = packet;
		request->number = request_num_counter++;
		strNcpy(request->secret, (char *)secret,
			sizeof(request->secret));

		/*
		 *	Remember the request.
		 */
		rl_add(request);

		/*
		 *	ADD IN "server identifier" from "listen"
		 *	directive!
		 */

		/*
		 *	The request passes many of our sanity checks.
		 *	From here on in, if anything goes wrong, we
		 *	send a reject message, instead of dropping the
		 *	packet.
		 *
		 *	Build the reply template from the request
		 *	template.
		 */
		rad_assert(request->reply == NULL);
		if ((request->reply = rad_alloc(0)) == NULL) {
			radlog(L_ERR, "No memory");
			exit(1);
		}
		request->reply->sockfd = request->packet->sockfd;
		request->reply->dst_ipaddr = request->packet->src_ipaddr;
		request->reply->src_ipaddr = request->packet->dst_ipaddr;
		request->reply->dst_port = request->packet->src_port;
		request->reply->src_port = request->packet->dst_port;
		request->reply->id = request->packet->id;
		request->reply->code = 0; /* UNKNOWN code */
		memcpy(request->reply->vector, request->packet->vector,
		       sizeof(request->reply->vector));
		request->reply->vps = NULL;
		request->reply->data = NULL;
		request->reply->data_len = 0;
	}

	return request;
}


/*
 *	The main guy.
 */
int main(int argc, char *argv[])
{
	REQUEST *request;
	RADIUS_PACKET *packet;
	u_char *secret;
	unsigned char buffer[4096];
	fd_set readfds;
	int argval;
	int pid;
	int max_fd;
	int status;
	struct timeval *tv = NULL;
	int spawn_flag = TRUE;
	int dont_fork = FALSE;
	int sig_hup_block = FALSE;

#ifdef HAVE_SIGACTION
	struct sigaction act;
#endif
	rad_listen_t *listener;

#ifdef OSFC2
	set_auth_parameters(argc,argv);
#endif

	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

	debug_flag = 0;
	spawn_flag = TRUE;
	radius_dir = strdup(RADIUS_DIR);

	/*
	 *	Ensure that the configuration is initialized.
	 */
	memset(&mainconfig, 0, sizeof(mainconfig));
	mainconfig.myip.af = AF_UNSPEC;
	mainconfig.port = -1;

#ifdef HAVE_SIGACTION
	memset(&act, 0, sizeof(act));
	act.sa_flags = 0 ;
	sigemptyset( &act.sa_mask ) ;
#endif

	/*  Process the options.  */
	while ((argval = getopt(argc, argv, "Aa:bcd:fg:hi:l:p:sSvxXyz")) != EOF) {

		switch(argval) {

			case 'A':
				log_auth_detail = TRUE;
				break;

			case 'a':
				if (radacct_dir) free(radacct_dir);
				radacct_dir = strdup(optarg);
				break;

			case 'c':
				/* ignore for backwards compatibility with Cistron */
				break;

			case 'd':
				if (radius_dir) free(radius_dir);
				radius_dir = strdup(optarg);
				break;

			case 'f':
				dont_fork = TRUE;
				break;

			case 'h':
				usage(0);
				break;

			case 'i':
				if (ip_hton(optarg, AF_INET, &mainconfig.myip) < 0) {
					fprintf(stderr, "radiusd: Invalid IP Address or hostname \"%s\"\n", optarg);
					exit(1);
				}
				break;

			case 'l':
				if ((strcmp(optarg, "stdout") == 0) ||
				    (strcmp(optarg, "stderr") == 0) ||
				    (strcmp(optarg, "syslog") == 0)) {
					fprintf(stderr, "radiusd: -l %s is unsupported.  Use log_destination in radiusd.conf\n", optarg);
					exit(1);
				}
				radlog_dir = strdup(optarg);
				break;

			case 'g':
				fprintf(stderr, "radiusd: -g is unsupported.  Use log_destination in radiusd.conf.\n");
				exit(1);
				break;

			case 'S':
				log_stripped_names++;
				break;

			case 'p':
				mainconfig.port = atoi(optarg);
				if ((mainconfig.port <= 0) ||
				    (mainconfig.port >= 65536)) {
					fprintf(stderr, "radiusd: Invalid port number %s\n", optarg);
					exit(1);
				}
				break;

			case 's':	/* Single process mode */
				spawn_flag = FALSE;
				dont_fork = TRUE;
				break;

			case 'v':
				version();
				break;

				/*
				 *  BIG debugging mode for users who are
				 *  TOO LAZY to type '-sfxxyz -l stdout' themselves.
				 */
			case 'X':
				spawn_flag = FALSE;
				dont_fork = TRUE;
				debug_flag += 2;
				mainconfig.log_auth = TRUE;
				mainconfig.log_auth_badpass = TRUE;
				mainconfig.log_auth_goodpass = TRUE;
				mainconfig.radlog_dest = RADLOG_STDOUT;
				break;

			case 'x':
				debug_flag++;
				break;

			case 'y':
				mainconfig.log_auth = TRUE;
				mainconfig.log_auth_badpass = TRUE;
				break;

			case 'z':
				mainconfig.log_auth_badpass = TRUE;
				mainconfig.log_auth_goodpass = TRUE;
				break;

			default:
				usage(1);
				break;
		}
	}

	/*
	 *	Get our PID.
	 */
	radius_pid = getpid();

	/*  Read the configuration files, BEFORE doing anything else.  */
	if (read_mainconfig(0) < 0) {
		exit(1);
	}

	/*
	 *	If we're NOT debugging, trap fatal signals, so we can
	 *	easily clean up after ourselves.
	 *
	 *	If we ARE debugging, don't trap them, so we can
	 *	dump core.
	 */
	if ((mainconfig.allow_core_dumps == FALSE) && (debug_flag == 0)) {
#ifdef SIGSEGV
#ifdef HAVE_SIGACTION
		act.sa_handler = sig_fatal;
		sigaction(SIGSEGV, &act, NULL);
#else
		signal(SIGSEGV, sig_fatal);
#endif
#endif
	}

	/*  Reload the modules.  */
	DEBUG2("radiusd:  entering modules setup");
	if (setup_modules() < 0) {
		radlog(L_ERR|L_CONS, "Errors setting up modules");
		exit(1);
	}

	/*  Initialize the request list.  */
	rl_init();

#ifdef WITH_SNMP
	if (mainconfig.do_snmp) radius_snmp_init();
#endif

	/*
	 *  Disconnect from session
	 */
	if (debug_flag == 0 && dont_fork == FALSE) {
		pid = fork();
		if(pid < 0) {
			radlog(L_ERR|L_CONS, "Couldn't fork");
			exit(1);
		}

		/*
		 *  The parent exits, so the child can run in the background.
		 */
		if(pid > 0) {
			exit(0);
		}
#ifdef HAVE_SETSID
		setsid();
#endif
	}

	/*
	 *  Ensure that we're using the CORRECT pid after forking,
	 *  NOT the one we started with.
	 */
	radius_pid = getpid();


	/*
	 *  Only write the PID file if we're running as a daemon.
	 *
	 *  And write it AFTER we've forked, so that we write the
	 *  correct PID.
	 */
	if (dont_fork == FALSE) {
		FILE *fp;

		fp = fopen(mainconfig.pid_file, "w");
		if (fp != NULL) {
			/*
			 *	FIXME: What about following symlinks,
			 *	and having it over-write a normal file?
			 */
			fprintf(fp, "%d\n", (int) radius_pid);
			fclose(fp);
		} else {
			radlog(L_ERR|L_CONS, "Failed creating PID file %s: %s\n",
			       mainconfig.pid_file, strerror(errno));
			exit(1);
		}
	}

	/*
	 *	If we're running as a daemon, close the default file
	 *	descriptors, AFTER forking.
	 */
	mainconfig.radlog_fd = -1;
	if (debug_flag) {
		mainconfig.radlog_fd = STDOUT_FILENO;
	} else {
		int devnull;

		devnull = open("/dev/null", O_RDWR);
		if (devnull < 0) {
			radlog(L_ERR|L_CONS, "Failed opening /dev/null: %s\n",
			       strerror(errno));
			exit(1);
		}
		dup2(devnull, STDIN_FILENO);
		if (mainconfig.radlog_dest == RADLOG_STDOUT) {
			mainconfig.radlog_fd = dup(STDOUT_FILENO);
		}
		dup2(devnull, STDOUT_FILENO);
		if (mainconfig.radlog_dest == RADLOG_STDERR) {
			mainconfig.radlog_fd = dup(STDERR_FILENO);
		}
		dup2(devnull, STDERR_FILENO);
		close(devnull);
	}

	/*
	 *	It's called the thread pool, but it does a little
	 *	more than that.
	 */
	thread_pool_init(spawn_flag);

	/*
	 *  Use linebuffered or unbuffered stdout if
	 *  the debug flag is on.
	 */
	if (debug_flag == TRUE)
		setlinebuf(stdout);

	/*
	 *	Print out which ports we're listening on.
	 */
	for (listener = mainconfig.listen;
	     listener != NULL;
	     listener = listener->next) {
		if ((listener->ipaddr.af == AF_INET) &&
		    (listener->ipaddr.ipaddr.ip4addr.s_addr == htonl(INADDR_ANY))) {
			strcpy(buffer, "*");
		} else if ((listener->ipaddr.af == AF_INET6) &&
			   (IN6_IS_ADDR_UNSPECIFIED(&listener->ipaddr.ipaddr))) {
			strcpy(buffer, "* (IPv6)");

		} else {
			ip_ntoh(&listener->ipaddr, buffer, sizeof(buffer));
		}
		
		switch (listener->type) {
		case RAD_LISTEN_AUTH:
			DEBUG("Listening on authentication address %s port %d",
			      buffer, listener->port);
			break;

		case RAD_LISTEN_ACCT:
			DEBUG("Listening on accounting addres %s port %d",
			      buffer, listener->port);
			break;

		case RAD_LISTEN_PROXY:
			DEBUG("Listening on proxy address %s port %d",
			      buffer, listener->port);
			break;

		default:
			break;
		}
	}

	/*
	 *	Now that we've set everything up, we can install the signal
	 *	handlers.  Before this, if we get any signal, we don't know
	 *	what to do, so we might as well do the default, and die.
	 */
	signal(SIGPIPE, SIG_IGN);
#ifdef HAVE_SIGACTION
	act.sa_handler = sig_hup;
	sigaction(SIGHUP, &act, NULL);
	act.sa_handler = sig_fatal;
	sigaction(SIGTERM, &act, NULL);
#else
	signal(SIGHUP, sig_hup);
	signal(SIGTERM, sig_fatal);
#endif
	/*
	 *	If we're debugging, then a CTRL-C will cause the
	 *	server to die immediately.  Use SIGTERM to shut down
	 *	the server cleanly in that case.
	 */
	if (debug_flag == 0) {
#ifdef HAVE_SIGACTION
	        act.sa_handler = sig_fatal;
		sigaction(SIGINT, &act, NULL);
		sigaction(SIGQUIT, &act, NULL);
#else
		signal(SIGINT, sig_fatal);
		signal(SIGQUIT, sig_fatal);
#endif
	}

	radlog(L_INFO, "Ready to process requests.");
	start_time = time(NULL);

	/*
	 *  Receive user requests
	 */
	for (;;) {
		/*
		 *	If we've been told to exit, then do so,
		 *	even if we have data waiting.
		 */
		if (do_exit) {
			DEBUG("Exiting...");

			/*
			 *	Ignore the TERM signal: we're about
			 *	to die.
			 */
			signal(SIGTERM, SIG_IGN);

			/*
			 *	Send a TERM signal to all associated
			 *	processes (including us, which gets
			 *	ignored.)
			 */
			kill(-radius_pid, SIGTERM);

			/*
			 *	FIXME: Kill child threads, and
			 *	clean up?
			 */

			/*
			 *	Detach any modules.
			 */
			detach_modules();

			/*
			 *	FIXME: clean up any active REQUEST
			 *	handles.
			 */

			/*
			 *	We're exiting, so we can delete the PID
			 *	file.  (If it doesn't exist, we can ignore
			 *	the error returned by unlink)
			 */
			if (dont_fork == FALSE) {
				unlink(mainconfig.pid_file);
			}

			/*
			 *	Free the configuration items.
			 */
			free_mainconfig();

			/*
			 *	SIGTERM gets do_exit=0,
			 *	and we want to exit cleanly.
			 *
			 *	Other signals make us exit
			 *	with an error status.
			 */
			exit(do_exit - 1);
		}

		if (need_reload) {
#ifdef HAVE_PTHREAD_H
			/*
			 *	Threads: wait for all threads to stop
			 *	processing before re-loading the
			 *	config, so we don't pull the rug out
			 *	from under them.
			 */
		        int max_wait = 0;
		        if (!spawn_flag) for(;;) {
			        /*
				 * Block until there are '0' threads
				 * with a REQUEST handle.
				 */
			        sig_hup_block = TRUE;
			        if( (total_active_threads() == 0) ||
				    (max_wait >= 5) ) {
					sig_hup_block = FALSE;
					break;
				}
				sleep(1);
				max_wait++;
			}
#endif
			if (read_mainconfig(TRUE) < 0) {
				exit(1);
			}

			/*  Reload the modules.  */
			DEBUG2("radiusd:  entering modules setup");
			if (setup_modules() < 0) {
				radlog(L_ERR|L_CONS, "Errors setting up modules");
				exit(1);
			}

			need_reload = FALSE;
			radlog(L_INFO, "Ready to process requests.");
		}

		FD_ZERO(&readfds);
		max_fd = 0;

		/*
		 *	Loop over all the listening FD's.
		 */
		for (listener = mainconfig.listen;
		     listener != NULL;
		     listener = listener->next) {
			FD_SET(listener->fd, &readfds);
			if (listener->fd > max_fd) max_fd = listener->fd;
		}

#ifdef WITH_SNMP
		if (mainconfig.do_snmp &&
		    (rad_snmp.smux_fd >= 0)) {
			FD_SET(rad_snmp.smux_fd, &readfds);
			if (rad_snmp.smux_fd > max_fd) max_fd = rad_snmp.smux_fd;
		}
#endif
		status = select(max_fd + 1, &readfds, NULL, NULL, tv);
		if (status == -1) {
			/*
			 *	On interrupts, we clean up the request
			 *	list.  We then continue with the loop,
			 *	so that if we're supposed to exit,
			 *	then the code at the start of the loop
			 *	catches that, and exits.
			 */
			if (errno == EINTR) {
#ifdef MEMORY_USE_DEBUGGING
				/*
				 *	Run the server in debugging mode,
				 *	without threads, and give it a
				 *	SIGHUP.  It will clean up after
				 *	itself, and any memory left over
				 *	should be allocated by C libraries,
				 *	and the like.
				 */
				detach_modules();
				rl_deinit();
				free_mainconfig();
				xlat_free();
				dict_free();
				exit(1);
#endif
				tv = rl_clean_list(time(NULL));
				continue;
			}
			radlog(L_ERR, "Unexpected error in select(): %s",
					strerror(errno));
			exit(1);
		}

		time_now = time(NULL);
#ifndef HAVE_PTHREAD_H
		/*
		 *	If there are no child threads, then there may
		 *	be child processes.  In that case, wait for
		 *	their exit status, and throw that exit status
		 *	away.  This helps get rid of zxombie children.
		 */
		while (waitpid(-1, &argval, WNOHANG) > 0) {
			/* do nothing */
		}
#endif

		/*
		 *	Loop over the open socket FD's, reading any data.
		 */
		for (listener = mainconfig.listen;
		     listener != NULL;
		     listener = listener->next) {
			RAD_REQUEST_FUNP fun;

			if (!FD_ISSET(listener->fd, &readfds))
				continue;
			/*
			 *  Receive the packet.
			 */
			if (sig_hup_block != FALSE) {
				continue;
			}
			packet = rad_recv(listener->fd);
			if (packet == NULL) {
				radlog(L_ERR, "%s", librad_errstr);
				continue;
			}

			RAD_SNMP_TYPE_INC(listener, total_requests);

			/*
			 *	FIXME: Move this next check into
			 *	the packet_ok() function, and add
			 *	a 'secret' to the RAIDUS_PACKET
			 *	data structure.  This involves changing
			 *	a bunch of code, but it's probably the
			 *	best thing to do.
			 */

			/*
			 *  Check if we know this client for
			 *  authentication and accounting.  Check if we know
			 *  this proxy for proxying.
			 */
			if (listener->type != RAD_LISTEN_PROXY) {
				RADCLIENT *cl;
				if ((cl = client_find(&packet->src_ipaddr)) == NULL) {
					RAD_SNMP_TYPE_INC(listener, total_invalid_requests);

					radlog(L_ERR, "Ignoring request from unknown client %s port %d",
					       inet_ntop(packet->src_ipaddr.af,
							 &packet->src_ipaddr.ipaddr,
							 buffer, sizeof(buffer)),
					packet->src_port);
					rad_free(&packet);
					continue;
				}
				secret = cl->secret;
			} else {    /* It came in on the proxy port */
				REALM *rl;

				if (packet->src_ipaddr.af != AF_INET) {
					rad_assert("PROXY IPV6 NOT SUPPORTED" == NULL);
				}

				if ((rl = realm_findbyaddr(packet->src_ipaddr.ipaddr.ip4addr.s_addr,
							   packet->src_port)) == NULL) {
					radlog(L_ERR, "Ignoring request from unknown home server %s port %d",
					       inet_ntop(packet->src_ipaddr.af,
							 &packet->src_ipaddr.ipaddr,
							 buffer, sizeof(buffer)),
					       packet->src_port);
					rad_free(&packet);
					continue;
				}

				/*
				 *	The secret isn't needed here,
				 *	as it's already in the old request
				 */
				secret = NULL;
			}

			/*
			 *	Do some simple checks before we process
			 *	the request.
			 */
			if ((fun = packet_ok(packet, listener)) == NULL) {
				rad_free(&packet);
				continue;
			}
			
			/*
			 *	Allocate a new request for packets from
			 *	our clients, OR find the old request,
			 *	for packets which are replies from a home
			 *	server.
			 */
			request = request_ok(packet, secret, listener);
			if (!request) {
				rad_free(&packet);
				continue;
			}

			/*
			 *	Drop the request into the thread pool,
			 *	and let the thread pool take care of
			 *	doing something with it.
			 */
			if (!thread_pool_addrequest(request, fun)) {
				/*
				 *	FIXME: Maybe just drop
				 *	the packet on the floor?
				 */
				request_reject(request, REQUEST_FAIL_NO_THREADS);
				request->finished = TRUE;
			}
		} /* loop over listening sockets*/

#ifdef WITH_SNMP
		if (mainconfig.do_snmp) {
			/*
			 *  After handling all authentication/accounting
			 *  requests, THEN process any pending SMUX/SNMP
			 *  queries.
			 *
			 *  Note that the handling is done in the main server,
			 *  which probably isn't a Good Thing.  It really
			 *  should be wrapped, and handled in a thread pool.
			 */
			if ((rad_snmp.smux_fd >= 0) &&
			    FD_ISSET(rad_snmp.smux_fd, &readfds) &&
			    (rad_snmp.smux_event == SMUX_READ)) {
				smux_read();
			}

			/*
			 *  If we've got to re-connect, then do so now,
			 *  before calling select again.
			 */
			if (rad_snmp.smux_event == SMUX_CONNECT) {
				smux_connect();
			}
		}
#endif

		/*
		 *  After processing all new requests,
		 *  check if we've got to delete old requests
		 *  from the request list.
		 */
		tv = rl_clean_list(time_now);
#ifdef HAVE_PTHREAD_H

		/*
		 *	Only clean the thread pool if we're spawning
		 *	child threads. 
		 */
		if (spawn_flag) {
			thread_pool_clean(time_now);
		}
#endif


	} /* loop forever */
}


/*
 *  Display the syntax for starting this program.
 */
static void usage(int status)
{
	FILE *output = status?stderr:stdout;

	fprintf(output,
			"Usage: %s [-a acct_dir] [-d db_dir] [-l log_dir] [-i address] [-AcfnsSvXxyz]\n", progname);
	fprintf(output, "Options:\n\n");
	fprintf(output, "  -a acct_dir     use accounting directory 'acct_dir'.\n");
	fprintf(output, "  -A              Log auth detail.\n");
	fprintf(output, "  -d raddb_dir    Configuration files are in \"raddbdir/*\".\n");
	fprintf(output, "  -f              Run as a foreground process, not a daemon.\n");
	fprintf(output, "  -h              Print this help message.\n");
	fprintf(output, "  -i ipaddr       Listen on ipaddr ONLY\n");
	fprintf(output, "  -l log_dir      Log file is \"log_dir/radius.log\" (not used in debug mode)\n");
	fprintf(output, "  -p port         Listen on port ONLY\n");
	fprintf(output, "  -s              Do not spawn child processes to handle requests.\n");
	fprintf(output, "  -S              Log stripped names.\n");
	fprintf(output, "  -v              Print server version information.\n");
	fprintf(output, "  -X              Turn on full debugging.\n");
	fprintf(output, "  -x              Turn on additional debugging. (-xx gives more debugging).\n");
	fprintf(output, "  -y              Log authentication failures, with password.\n");
	fprintf(output, "  -z              Log authentication successes, with password.\n");
	exit(status);
}


/*
 *	We got a fatal signal.
 */
static void sig_fatal(int sig)
{
	switch(sig) {
		case SIGSEGV:
			/* We can't really do anything intelligent here so just die */
			exit(1);
		case SIGTERM:
			do_exit = 1;
			break;
		default:
			do_exit = 2;
			break;
	}
}


/*
 *  We got the hangup signal.
 *  Re-read the configuration files.
 */
/*ARGSUSED*/
static void sig_hup(int sig)
{
	sig = sig; /* -Wunused */

	reset_signal(SIGHUP, sig_hup);

	/*
	 *  Only do the reload if we're the main server, both
	 *  for processes, and for threads.
	 */
	if (getpid() == radius_pid) {
		need_reload = TRUE;
	}
#ifdef WITH_SNMP
	if (mainconfig.do_snmp) {
		rad_snmp.smux_failures = 0;
		rad_snmp.smux_event = SMUX_CONNECT;
	}
#endif
}


/*
 *	Process and reply to a server-status request.
 *	Like rad_authenticate and rad_accounting this should
 *	live in it's own file but it's so small we don't bother.
 */
static int rad_status_server(REQUEST *request)
{
	char		reply_msg[64];
	time_t		t;
	VALUE_PAIR	*vp;

	/*
	 *	Reply with an ACK. We might want to add some more
	 *	interesting reply attributes, such as server uptime.
	 */
	t = request->timestamp - start_time;
	sprintf(reply_msg, "FreeRADIUS up %d day%s, %02d:%02d",
		(int)(t / 86400), (t / 86400) == 1 ? "" : "s",
		(int)((t / 3600) % 24), (int)(t / 60) % 60);
	request->reply->code = PW_AUTHENTICATION_ACK;

	vp = pairmake("Reply-Message", reply_msg, T_OP_SET);
	pairadd(&request->reply->vps, vp); /* don't need to check if !vp */

	return 0;
}
