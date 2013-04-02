/*
 * sql_fbapi.h Part of Firebird rlm_sql driver
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
 * Copyright 2006  The FreeRADIUS server project
 * Copyright 2006  Vitaly Bodzhgua <vitaly@eastera.net>
 */


#ifndef _SQL_FBAPI_H_
#define _SQL_FBAPI_H_

RCSIDH(sql_fbapi_h, "$Id$")

#include <stdlib.h>
#include <string.h>
#include <ibase.h>

#include <freeradius-devel/radiusd.h>
#include "rlm_sql.h"

#define IS_ISC_ERROR(status)  (status[0] == 1 && status[1])

#define DEADLOCK_SQL_CODE	-913
#define DOWN_SQL_CODE		-902

typedef struct rlm_sql_firebird_conn {
	isc_db_handle dbh;
	isc_stmt_handle stmt;
	isc_tr_handle trh;
	ISC_STATUS status[20];
	ISC_LONG sql_code;
	XSQLDA *sqlda_out;
	int sql_dialect;
	int statement_type;
	char *tpb;
	int tpb_len;
	char *dpb;
	int dpb_len;
	char *lasterror;

	rlm_sql_row_t row;
	int *row_sizes;
	int row_fcount;

#ifdef _PTHREAD_H
	pthread_mutex_t mut;
#endif
} rlm_sql_firebird_conn_t;


int fb_free_result(rlm_sql_firebird_conn_t *conn);
int fb_lasterror(rlm_sql_firebird_conn_t *conn);
int fb_connect(rlm_sql_firebird_conn_t *conn, rlm_sql_config_t *config);
int fb_disconnect(rlm_sql_firebird_conn_t *conn);
int fb_sql_query(rlm_sql_firebird_conn_t *conn, char *sqlstr);
int fb_affected_rows(rlm_sql_firebird_conn_t *conn);
int fb_fetch(rlm_sql_firebird_conn_t *conn);
void fb_free_statement(rlm_sql_firebird_conn_t *conn);
int fb_close_cursor(rlm_sql_firebird_conn_t *conn);
int fb_rollback(rlm_sql_firebird_conn_t *conn);
int fb_commit(rlm_sql_firebird_conn_t *conn);
void fb_store_row(rlm_sql_firebird_conn_t *conn);

#endif
