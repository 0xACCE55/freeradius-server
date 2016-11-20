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
#ifndef _FR_TRACK_H
#define _FR_TRACK_H
/**
 * $Id$
 *
 * @file util/track.h
 * @brief RADIUS packet tracking.
 *
 * @copyright 2016 Alan DeKok <aland@freeradius.org>
 */
RCSIDH(track_h, "$Id$")

#include <freeradius-devel/util/channel.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fr_tracking_t fr_tracking_t;

/**
 *  An entry for the tracking table.  It contains the minimum
 *  information required to track RADIUS packets.
 */
typedef struct fr_tracking_entry_t {
	fr_time_t		timestamp;	//!< when received
	fr_channel_data_t	*reply;		//!< the reply (if any)
	uint8_t			data[18];	//!< 2 byte length + authentication vector
} fr_tracking_entry_t;

/**
 *  The status of an insert.
 */
typedef enum fr_tracking_status_t {
	FR_TRACKING_UNUSED = 0,
	FR_TRACKING_NEW,
	FR_TRACKING_SAME,
	FR_TRACKING_DIFFERENT,
} fr_tracking_status_t;

fr_tracking_t *fr_radius_tracking_create(TALLOC_CTX *ctx);
int fr_radius_tracking_entry_delete(fr_tracking_t *ft, uint8_t id) CC_HINT(nonnull);
fr_tracking_status_t fr_radius_tracking_entry_insert(fr_tracking_t *ft, uint8_t *packet, fr_time_t timestamp,
						     fr_tracking_entry_t **p_entry) CC_HINT(nonnull);
int fr_radius_tracking_entry_reply(fr_tracking_t *ft, uint8_t id,
				   fr_channel_data_t *cd) CC_HINT(nonnull);

#ifdef __cplusplus
}
#endif

#endif /* _FR_TRACK_H */
