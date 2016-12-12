/*
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
 */

/**
 * $Id$
 *
 * @brief Control-plane signaling
 * @file util/control.c
 *
 * @copyright 2016 Alan DeKok <aland@freeradius.org>
 */
RCSID("$Id$")

#include <freeradius-devel/util/control.h>
#include <freeradius-devel/util/ring_buffer.h>
#include <freeradius-devel/rad_assert.h>

#include <string.h>
#include <sys/event.h>

#define FR_CONTROL_SIGNAL	(1024)
#define FR_CONTROL_MAX_MESSAGES (128)
#define FR_CONTROL_MAX_SIZE	(64)

/**
 *  Status of control messages
 */
typedef enum fr_control_message_status_t {
	FR_CONTROL_MESSAGE_FREE = 0,			//!< the message is free
	FR_CONTROL_MESSAGE_USED,			//!< the message is used (set only by originator)
	FR_CONTROL_MESSAGE_DONE				//!< the message is done (set only by receiver)
} fr_control_message_status_t;

/**
 *  The header for the control message
 */
typedef struct fr_control_message_t {
	fr_control_message_status_t	status;		//!< status of this message
	size_t				data_size;     	//!< size of the data we're sending
} fr_control_message_t;

/**
 *  The control structure.
 */
struct fr_control_t {
	int			kq;			//!< destination KQ

	fr_atomic_queue_t	*aq;			//!< destination AQ

	fr_ring_buffer_t	*rb;			//!< a ring buffer containing my messages to send.
};


/** Create a control-plane signaling path.
 *
 * @param[in] ctx the talloc context
 * @param[in] kq the KQ descriptor where we will be sending signals
 * @param[in] aq the atomic queue where we will be pushing message data
 * @return
 *	- NULL on error
 *	- fr_control_t on success
 */
fr_control_t *fr_control_create(TALLOC_CTX *ctx, int kq, fr_atomic_queue_t *aq)
{
	fr_control_t *c;
	struct kevent kev;

	c = talloc_zero(ctx, fr_control_t);
	if (!c) return NULL;

	c->kq = kq;
	c->aq = aq;

	c->rb = fr_ring_buffer_create(c, FR_CONTROL_MAX_SIZE * FR_CONTROL_MAX_MESSAGES);
	if (!c->rb) {
		talloc_free(c);
		return NULL;

	}

	/*
	 *	Tell the KQ to listen on our events.
	 *
	 *	We COULD overload the "ident" field with our channel
	 *	number, followed by the actual signal we're sending.
	 *	This would work.  The downside is that it would
	 *	require N*M EVFILT_USER kevents to be registered,
	 *	which is bad
	 *
	 *	The implementation here is perhaps a bit less optimal,
	 *	but it's clean, and it works.
	 */
	EV_SET(&kev, FR_CONTROL_SIGNAL, EVFILT_USER, EV_ADD | EV_CLEAR, NOTE_FFNOP, 0, NULL);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
		talloc_free(c);
		return NULL;
	}

	return c;
}


/** Clean up messages in a control-plane buffer
 *
 *  Find the oldest messages which are marked FR_CONTROL_MESSAGE_DONE,
 *  and mark them FR_CONTROL_MESSAGE_FREE.
 *
 * @param[in] c the fr_control_t
 * @return
 *	- <0 there are still messages used
 *	- 0 the control list is empty.
 */
int fr_control_gc(fr_control_t *c)
{
	while (true) {
		size_t room, message_size;
		fr_control_message_t *m;

		(void) fr_ring_buffer_start(c->rb, (uint8_t **) &m, &room);
		if (room == 0) break;

		rad_assert(m != NULL);
		rad_assert(room >= sizeof(*m));

		rad_assert(m->status != FR_CONTROL_MESSAGE_FREE);

		if (m->status != FR_CONTROL_MESSAGE_DONE) break;

		m->status = FR_CONTROL_MESSAGE_FREE;

		/*
		 *	Each message is aligned to a 64-byte boundary,
		 *	for cache contention issues.
		 */
		message_size = sizeof(*m);
		message_size += m->data_size;
		message_size += 63;
		message_size &= ~(size_t) 63;
		fr_ring_buffer_free(c->rb, message_size);
	}

	/*
	 *	Maybe we failed to garbage collect everything?
	 */
	if (fr_ring_buffer_used(c->rb) > 0) {
		return -1;
	}

	return 0;
}

/** Free a control structure
 *
 *  This function really only calls the underlying "garbage collect".
 *
 * @param[in] c the control structure
 */
void fr_control_free(fr_control_t *c)
{
#ifndef NDEBUG
	(void) talloc_get_type_abort(c, fr_control_t);
#endif

	(void) fr_control_gc(c);

	rad_assert(fr_ring_buffer_used(c->rb) == 0);

	talloc_free(c);
}


/** Allocate a control message
 *
 * @param[in] c the control structure
 * @param[in] data the data to write to the control plane
 * @param[in] data_size the size of the data to write to the control plane.
 * @return
 *	- NULL on error
 *	- fr_message_t on success
 */
static fr_control_message_t *fr_control_message_alloc(fr_control_t *c, void *data, size_t data_size)
{
	size_t message_size;
	fr_control_message_t *m;
	uint8_t *p;

	message_size = sizeof(*m);
	message_size += data_size;
	message_size += 63;
	message_size &= ~(size_t) 63;

	m = (fr_control_message_t *) fr_ring_buffer_alloc(c->rb, message_size);
	if (!m) {
		(void) fr_control_gc(c);
		m = (fr_control_message_t *) fr_ring_buffer_alloc(c->rb, message_size);
		if (!m) return NULL;
	}

	m->status = FR_CONTROL_MESSAGE_USED;
	m->data_size = data_size;

	p = (uint8_t *) m;
	memcpy(p + sizeof(*m), data, data_size);

	return m;

}


/** Push a control-plane message
 *
 *  This function is called ONLY from the originating thread.
 *
 * @param[in] c the control structure
 * @param[in] data the data to write to the control plane
 * @param[in] data_size the size of the data to write to the control plane.
 * @return
 *	- <0 on error
 *	- 0 on success
 */
int fr_control_message_push(fr_control_t *c, void *data, size_t data_size)
{
	fr_control_message_t *m;

#ifndef NDEBUG
	(void) talloc_get_type_abort(c, fr_control_t);
#endif

	/*
	 *	Get a message.  If we can't get one, do garbage
	 *	collection.  Get another, and if that fails, we're
	 *	done.
	 */
	m = fr_control_message_alloc(c, data, data_size);
	if (!m) {
		(void) fr_control_gc(c);
		m = fr_control_message_alloc(c, data, data_size);
		if (!m) return -1;
	}

	if (!fr_atomic_queue_push(c->aq, m)) {
		m->status = FR_CONTROL_MESSAGE_DONE;
		return -1;
	}

	return 0;
}

/** Send a control-plane message
 *
 *  This function is called ONLY from the originating thread.
 *
 * @param[in] c the control structure
 * @param[in] data the data to write to the control plane
 * @param[in] data_size the size of the data to write to the control plane.
 * @return
 *	- <0 on error
 *	- 0 on success
 */
int fr_control_message_send(fr_control_t *c, void *data, size_t data_size)
{
	struct kevent kev;

#ifndef NDEBUG
	(void) talloc_get_type_abort(c, fr_control_t);
#endif

	if (fr_control_message_push(c, data, data_size) < 0) {
		return -1;
	}

	EV_SET(&kev, FR_CONTROL_SIGNAL, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFNOP, 0, NULL);
	return kevent(c->kq, &kev, 1, NULL, 0, NULL);
}


/** Pop control-plane message
 *
 *  This function is called ONLY from the receiving thread.
 *
 * @param[in] aq the recipients atomic queue for control-plane messages
 * @param[in,out] data where the data is stored
 * @param[in] data_size the size of the buffer where we store the data.
 * @return
 *	- <0 the size of the data we need to read the next message
 *	- 0 this kevent is not for us.
 *	- >0 the amount of data we've read
 */
ssize_t fr_control_message_pop(fr_atomic_queue_t *aq, void *data, size_t data_size)
{
	uint8_t *p;
	fr_control_message_t *m;

	if (!fr_atomic_queue_pop(aq, (void **) &m)) return 0;

	rad_assert(m->status == FR_CONTROL_MESSAGE_USED);

	/*
	 *	There isn't enough room to store the data, die.
	 */
	if (data_size < m->data_size) {
		return -(m->data_size);
	}

	p = (uint8_t *) m;
	data_size = m->data_size;
	memcpy(data, p + sizeof(*m), data_size);


	m->status = FR_CONTROL_MESSAGE_DONE;
	return data_size;
}


/** Receive a control-plane message
 *
 *  This function is called ONLY from the receiving thread.
 *
 * @param[in] aq the recipients atomic queue for control-plane messages
 * @param[in] kev the kevent for this receiver
 * @param[in,out] data where the data is stored
 * @param[in] data_size the size of the buffer where we store the data.
 * @return
 *	- <0 the size of the data we need to read the next message
 *	- 0 this kevent is not for us.
 *	- >0 the amount of data we've read
 */
ssize_t fr_control_message_receive(fr_atomic_queue_t *aq, struct kevent const *kev, void *data, size_t data_size)
{
	if (kev->ident != FR_CONTROL_SIGNAL) return 0;

	return fr_control_message_pop(aq, data, data_size);
}
