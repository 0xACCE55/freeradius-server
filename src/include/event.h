/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#ifndef _FR_EVENT_H
#define _FR_EVENT_H
/**
 * $Id$
 *
 * @file include/event.h
 * @brief A simple event queue.
 *
 * @copyright 2007  The FreeRADIUS server project
 * @copyright 2007  Alan DeKok <aland@deployingradius.com>
 */
RCSIDH(event_h, "$Id$")

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fr_event_list_t fr_event_list_t;
typedef struct fr_event_t fr_event_t;

typedef	void (*fr_event_callback_t)(void *, struct timeval *now);
typedef	void (*fr_event_status_t)(struct timeval *);
typedef void (*fr_event_fd_handler_t)(fr_event_list_t *el, int sock, void *ctx);

fr_event_list_t *fr_event_list_create(TALLOC_CTX *ctx, fr_event_status_t status);

int fr_event_list_num_fds(fr_event_list_t *el);
int fr_event_list_num_elements(fr_event_list_t *el);

int fr_event_insert(fr_event_list_t *el,
		    fr_event_callback_t callback,
		    void *ctx, struct timeval *when, fr_event_t **parent);
int fr_event_delete(fr_event_list_t *el, fr_event_t **parent);

int fr_event_run(fr_event_list_t *el, struct timeval *when);

int fr_event_now(fr_event_list_t *el, struct timeval *when);

int fr_event_fd_insert(fr_event_list_t *el, int fd, fr_event_fd_handler_t handler, void *ctx);

int fr_event_fd_delete(fr_event_list_t *el, int fd);

int fr_event_check(fr_event_list_t *el, bool wait);
int fr_event_service(fr_event_list_t *el);
int fr_event_loop(fr_event_list_t *el);
void fr_event_loop_exit(fr_event_list_t *el, int code);
bool fr_event_loop_exiting(fr_event_list_t *el);

#ifdef __cplusplus
}
#endif
#endif /* _FR_EVENT_H */
