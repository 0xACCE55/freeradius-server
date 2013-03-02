/*
 * sql_firebird.c Part of Firebird rlm_sql driver
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

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include "sql_fbapi.h"


/* Forward declarations */
static const char *sql_error(rlm_sql_handle_t *handle, rlm_sql_config_t *config);
static int sql_free_result(rlm_sql_handle_t *handle, rlm_sql_config_t *config);
static int sql_affected_rows(rlm_sql_handle_t *handle, rlm_sql_config_t *config);
static int sql_num_fields(rlm_sql_handle_t *handle, rlm_sql_config_t *config);
static int sql_finish_query(rlm_sql_handle_t *handle, rlm_sql_config_t *config);

/** Establish connection to the db
 *
 */
static int sql_init_socket(rlm_sql_handle_t *handle, rlm_sql_config_t *config) {
	rlm_sql_firebird_sock	*firebird_sock;
	
	long res;

	if (!handle->conn) {
		handle->conn = rad_malloc(sizeof(rlm_sql_firebird_sock));
		if (!handle->conn) {
			return -1;
		}
	}

	firebird_sock = handle->conn;

	res = fb_init_socket(firebird_sock);
	if (res) {
		return -1;
	}
	
	if (fb_connect(firebird_sock,config)) {
		radlog(L_ERR, "rlm_sql_firebird: Connection failed %s\n",
		       firebird_sock->lasterror);
		       
		return SQL_DOWN;
	}

	return 0;
}

/** Free socket and private connection data
 *
 */
static int sql_destroy_socket(rlm_sql_handle_t *handle,
			      rlm_sql_config_t *config)
{
	free(handle->conn);
	
	handle->conn = NULL;
	
	return 0;
}


/** Issue a non-SELECT query (ie: update/delete/insert) to the database.
 *
 */
static int sql_query(rlm_sql_handle_t *handle, UNUSED rlm_sql_config_t *config,
		     char *querystr) {
	rlm_sql_firebird_sock *firebird_sock = handle->conn;
	
	int deadlock = 0;

#ifdef _PTHREAD_H
	pthread_mutex_lock(&firebird_sock->mut);
#endif

	try_again:
	/* 
	 *	Try again query when deadlock, beacuse in any case it
	 *	will be retried.
	 */
 	if (fb_sql_query(firebird_sock,querystr)) {
		/* but may be lost for short sessions */
   		if ((firebird_sock->sql_code == DEADLOCK_SQL_CODE) &&
   		    !deadlock) {
	  		radlog(L_DBG,"sock_id deadlock. Retry query %s",
	  		       querystr);
	  		
			/*
			 *	@todo For non READ_COMMITED transactions put 
			 *	rollback here
			 *	fb_rollback(sock);
			 */
	  		deadlock = 1;
	  		goto try_again;
	  	}
  	
		radlog(L_ERR, "sock_id rlm_sql_firebird,sql_query error: "
		       "sql_code=%li, error='%s', query=%s",
		       (long int) firebird_sock->sql_code,
		       firebird_sock->lasterror,
		       querystr);

		if (firebird_sock->sql_code == DOWN_SQL_CODE) {
			return SQL_DOWN;
		}
	
		/* Free problem query */
		if (fb_rollback(firebird_sock)) {
			//assume the network is down if rollback had failed
			radlog(L_ERR,"Fail to rollback transaction after "
			       "previous error. Error: %s",
			       firebird_sock->lasterror);
		
			return SQL_DOWN;
		}
		//   firebird_sock->in_use=0;
		return -1;
   	}

	if (firebird_sock->statement_type != isc_info_sql_stmt_select) {
		if (fb_commit(firebird_sock)) {
			return -1;
		}
	}

	return 0;
}

/** Issue a select query to the database.
 *
 */
static int sql_select_query(rlm_sql_handle_t *handle, rlm_sql_config_t *config,
			    char *querystr) {
	return sql_query(handle, config, querystr);
}

/** Returns a result set for the query.
 *
 */
static int sql_store_result(UNUSED rlm_sql_handle_t *handle,
			    UNUSED rlm_sql_config_t *config) {
	return 0;
}

/** Returns number of columns from query.
 *
 */
static int sql_num_fields(rlm_sql_handle_t *handle,
			  UNUSED rlm_sql_config_t *config) {
	return ((rlm_sql_firebird_sock *) handle->conn)->sqlda_out->sqld;
}

/** Returns number of rows in query.
 *
 */
static int sql_num_rows(rlm_sql_handle_t *handle, rlm_sql_config_t *config) {
	return sql_affected_rows(handle, config);
}

/** Returns an individual row.
 *
 */
static int sql_fetch_row(rlm_sql_handle_t *handle, rlm_sql_config_t *config) {
	rlm_sql_firebird_sock *firebird_sock = handle->conn;
	int res;
	
	handle->row = NULL;
	
	if (firebird_sock->statement_type != isc_info_sql_stmt_exec_procedure) {
		res = fb_fetch(firebird_sock);
		if (res == 100) {
			return 0;
	 	}
	 	
	 	if (res) {
	  		radlog(L_ERR, "rlm_sql_firebird. Fetch problem:'%s'",
	  		       firebird_sock->lasterror);
	  		       
	   		return -1;
	 	}
	} else {
		firebird_sock->statement_type=0;
	}
	
	fb_store_row(firebird_sock);

	handle->row = firebird_sock->row;
	
	return 0;
}

/** End the select query, such as freeing memory or result.
 *
 */
static int sql_finish_select_query(rlm_sql_handle_t *handle,
				   UNUSED rlm_sql_config_t *config) {
				   
	rlm_sql_firebird_sock *sock = (rlm_sql_firebird_sock *) handle->conn;
	
	fb_commit(sock);
	fb_close_cursor(sock);
	
	return 0;
}

/** End the query
 *
 */
static int sql_finish_query(rlm_sql_handle_t *handle,
			    rlm_sql_config_t *config) {
	sql_free_result(handle, config);
	
	return 0;
}

/** Frees memory allocated for a result set.
 *
 */
static int sql_free_result(UNUSED rlm_sql_handle_t *handle,
			   UNUSED rlm_sql_config_t *config) {
	return 0;
}

/** Closes an open database connection and cleans up any open handles.
 *
 */
static int sql_close(rlm_sql_handle_t *handle, rlm_sql_config_t *config) {
	fb_destroy_socket((rlm_sql_firebird_sock *) handle->conn);
	return 0;
}

/** Returns error associated with connection.
 *
 */
static const char *sql_error(rlm_sql_handle_t *handle,
			     UNUSED rlm_sql_config_t *config) {
	rlm_sql_firebird_sock *firebird_sock = handle->conn;
	
	return firebird_sock->lasterror;
}

/** Return the number of rows affected by the query (update, or insert)
 *
 */
static int sql_affected_rows(rlm_sql_handle_t *handle, rlm_sql_config_t *config) {
	int affected_rows=fb_affected_rows(handle->conn);
	
	if (affected_rows < 0) {
		radlog(L_ERR, "sql_affected_rows, rlm_sql_firebird. error:%s\n",
		       sql_error(handle,config));
	}
	
	return affected_rows;
}

/* Exported to rlm_sql */
rlm_sql_module_t rlm_sql_firebird = {
	"rlm_sql_firebird",
	sql_init_socket,
	sql_destroy_socket,
	sql_query,
	sql_select_query,
	sql_store_result,
	sql_num_fields,
	sql_num_rows,
	sql_fetch_row,
	sql_free_result,
	sql_error,
	sql_close,
	sql_finish_query,
	sql_finish_select_query,
	sql_affected_rows
};
