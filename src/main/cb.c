/*
 * cb.c
 *
 * Version:     $Id$
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
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2001  hereUare Communications, Inc. <raghud@hereuare.com>
 * Copyright 2006  The FreeRADIUS server project
 */

RCSID("$Id$")
USES_APPLE_DEPRECATED_API	/* OpenSSL API has been deprecated by Apple */

#include <freeradius-devel/radiusd.h>

#ifdef WITH_TLS
void cbtls_info(SSL const *ssl, int where, int ret)
{
	char const *str, *state;
	REQUEST *request = SSL_get_ex_data(ssl, FR_TLS_EX_INDEX_REQUEST);

	if ((where & ~SSL_ST_MASK) & SSL_ST_CONNECT) {
		str = "connect";
	} else if (((where & ~SSL_ST_MASK)) & SSL_ST_ACCEPT) {
		str = "accept";
	} else {
		str = NULL;
	}

	state = SSL_state_string_long(ssl);
	state = state ? state : "<none>";

	if ((where & SSL_CB_LOOP) || (where & SSL_CB_HANDSHAKE_START) || (where & SSL_CB_HANDSHAKE_DONE)) {
		if (str) {
			RDEBUG2("%s: Handshake state \"%s\"", str, state);
		} else {
			RDEBUG2("Handshake state \"%s\"", state);
		}
		return;
	}

	if (where & SSL_CB_ALERT) {
		if ((ret & 0xff) == SSL_AD_CLOSE_NOTIFY) return;

		if (where & SSL_CB_READ) {
			REDEBUG("Client sent %s TLS alert: %s", SSL_alert_type_string_long(ret),
			       SSL_alert_desc_string_long(ret));

			/*
			 *	Offer helpful advice... Should be expanded.
			 */
			switch (ret & 0xff) {
			case TLS1_AD_UNKNOWN_CA:
				REDEBUG("Verify client has copy of CA certificate, and trusts CA");
				break;

			default:
				break;
			}
		} else {
			REDEBUG("Sending client %s TLS alert: %s %i",  SSL_alert_type_string_long(ret),
			       SSL_alert_desc_string_long(ret), ret & 0xff);
		}
		return;
	}

	if (where & SSL_CB_EXIT) {
		if (ret == 0) {
			REDEBUG("%s: Handshake exit state \"%s\"", str, state);
			return;
		}

		if (ret < 0) {
			if (SSL_want_read(ssl)) {
				RDEBUG2("%s: Need to read more data: %s", str, state);
				return;
			}
			REDEBUG("tls: %s: Handshake exit state \"%s\"", str, state);
		}
	}
}

/*
 *	Fill in our 'info' with TLS data.
 */
void cbtls_msg(int write_p, int msg_version, int content_type,
	       void const *inbuf, size_t len,
	       SSL *ssl UNUSED, void *arg)
{
	uint8_t const *buf = inbuf;
	tls_session_t *state = (tls_session_t *)arg;

	/*
	 *	OpenSSL 1.0.2 calls this function with 'pseudo'
	 *	content types.  Which breaks our tracking of
	 *	the SSL Session state.
	 */
	if ((msg_version == 0) && (content_type > UINT8_MAX)) {
		DEBUG4("Ignoring cbtls_msg call with pseudo content type %i, version %i",
		       content_type, msg_version);
		return;
	}

	if ((write_p != 0) && (write_p != 1)) {
		DEBUG4("Ignoring cbtls_msg call with invalid write_p %d", write_p);
		return;
	}

	/*
	 *	Work around bug #298, where we may be called with a NULL
	 *	argument.  We should really log a serious error
	 */
	if (!state) return;

	/*
	 *	0 - received (from peer)
	 *	1 - sending (to peer)
	 */
	state->info.origin = write_p;
	state->info.content_type = content_type;
	state->info.record_len = len;
	state->info.initialized = true;

	if (content_type == SSL3_RT_ALERT) {
		state->info.alert_level = buf[0];
		state->info.alert_description = buf[1];
		state->info.handshake_type = 0x00;

	} else if (content_type == SSL3_RT_HANDSHAKE) {
		state->info.handshake_type = buf[0];
		state->info.alert_level = 0x00;
		state->info.alert_description = 0x00;

#ifdef SSL3_RT_HEARTBEAT
	} else if (content_type == TLS1_RT_HEARTBEAT) {
		uint8_t *p = buf;

		if ((len >= 3) && (p[0] == 1)) {
			size_t payload_len;

			payload_len = (p[1] << 8) | p[2];

			if ((payload_len + 3) > len) {
				state->invalid_hb_used = true;
				ERROR("OpenSSL Heartbeat attack detected.  Closing connection");
				return;
			}
		}
#endif
	}
	tls_session_information(state);
}

int cbtls_password(char *buf,
		   int num UNUSED,
		   int rwflag UNUSED,
		   void *userdata)
{
	if (!userdata) {
		ERROR("Certificate encrypted but no private_key_password configured");
		return 0;
	}

	strcpy(buf, (char *)userdata);
	return(strlen((char *)userdata));
}

#endif
