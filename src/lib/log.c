/*
 * log.c	Functions in the library call radlib_log() which
 *		does internal logging.
 *
 * Version:	$Id$
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2000,2006  The FreeRADIUS server project
 */

RCSID("$Id$")

#include <freeradius-devel/libradius.h>

/*
 *	Are we using glibc or a close relative?
 */
#ifdef HAVE_FEATURES_H
#  include <features.h>
#endif

#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
#endif

#define FR_STRERROR_BUFSIZE (2048)

fr_thread_local_setup(char *, fr_strerror_buffer)	/* macro */
fr_thread_local_setup(char *, fr_syserror_buffer)	/* macro */

#ifndef NDEBUG
/** POSIX-2008 errno macros
 *
 * Non-POSIX macros may be added, but you must check they're defined.
 */
static char const *fr_errno_macro_names[] = {
	[E2BIG] = "E2BIG",
	[EACCES] = "EACCES",
	[EADDRINUSE] = "EADDRINUSE",
	[EADDRNOTAVAIL] = "EADDRNOTAVAIL",
	[EAFNOSUPPORT] = "EAFNOSUPPORT",
#if EWOULDBLOCK == EAGAIN
	[EWOULDBLOCK] = "EWOULDBLOCK or EAGAIN",
#else
	[EAGAIN] = "EAGAIN",
	[EWOULDBLOCK] = "EWOULDBLOCK",
#endif
	[EALREADY] = "EALREADY",
	[EBADF] = "EBADF",
	[EBADMSG] = "EBADMSG",
	[EBUSY] = "EBUSY",
	[ECANCELED] = "ECANCELED",
	[ECHILD] = "ECHILD",
	[ECONNABORTED] = "ECONNABORTED",
	[ECONNREFUSED] = "ECONNREFUSED",
	[ECONNRESET] = "ECONNRESET",
	[EDEADLK] = "EDEADLK",
	[EDESTADDRREQ] = "EDESTADDRREQ",
	[EDOM] = "EDOM",
	[EDQUOT] = "EDQUOT",
	[EEXIST] = "EEXIST",
	[EFAULT] = "EFAULT",
	[EFBIG] = "EFBIG",
	[EHOSTUNREACH] = "EHOSTUNREACH",
	[EIDRM] = "EIDRM",
	[EILSEQ] = "EILSEQ",
	[EINPROGRESS] = "EINPROGRESS",
	[EINTR] = "EINTR",
	[EINVAL] = "EINVAL",
	[EIO] = "EIO",
	[EISCONN] = "EISCONN",
	[EISDIR] = "EISDIR",
	[ELOOP] = "ELOOP",
	[EMFILE] = "EMFILE",
	[EMLINK] = "EMLINK",
	[EMSGSIZE] = "EMSGSIZE",
	[EMULTIHOP] = "EMULTIHOP",
	[ENAMETOOLONG] = "ENAMETOOLONG",
	[ENETDOWN] = "ENETDOWN",
	[ENETRESET] = "ENETRESET",
	[ENETUNREACH] = "ENETUNREACH",
	[ENFILE] = "ENFILE",
	[ENOBUFS] = "ENOBUFS",
#ifdef ENODATA
	[ENODATA] = "ENODATA",
#endif
	[ENODEV] = "ENODEV",
	[ENOENT] = "ENOENT",
	[ENOEXEC] = "ENOEXEC",
	[ENOLCK] = "ENOLCK",
	[ENOLINK] = "ENOLINK",
	[ENOMEM] = "ENOMEM",
	[ENOMSG] = "ENOMSG",
	[ENOPROTOOPT] = "ENOPROTOOPT",
	[ENOSPC] = "ENOSPC",
#ifdef ENOSR
	[ENOSR] = "ENOSR",
#endif
#ifdef ENOSTR
	[ENOSTR] = "ENOSTR",
#endif
	[ENOSYS] = "ENOSYS",
	[ENOTCONN] = "ENOTCONN",
	[ENOTDIR] = "ENOTDIR",
	[ENOTEMPTY] = "ENOTEMPTY",
#ifdef ENOTRECOVERABLE
	[ENOTRECOVERABLE] = "ENOTRECOVERABLE",
#endif
	[ENOTSOCK] = "ENOTSOCK",
#if ENOTSUP == EOPNOTSUPP
	[ENOTSUP] = "ENOTSUP or EOPNOTSUPP",
#else
	[ENOTSUP] = "ENOTSUP",
	[EOPNOTSUPP] = "EOPNOTSUPP",
#endif
	[ENOTTY] = "ENOTTY",
	[ENXIO] = "ENXIO",
	[EOVERFLOW] = "EOVERFLOW",
#ifdef EOWNERDEAD
	[EOWNERDEAD] = "EOWNERDEAD",
#endif
	[EPERM] = "EPERM",
	[EPIPE] = "EPIPE",
	[EPROTO] = "EPROTO",
	[EPROTONOSUPPORT] = "EPROTONOSUPPORT",
	[EPROTOTYPE] = "EPROTOTYPE",
	[ERANGE] = "ERANGE",
	[EROFS] = "EROFS",
	[ESPIPE] = "ESPIPE",
	[ESRCH] = "ESRCH",
	[ESTALE] = "ESTALE",
#ifdef ETIME
	[ETIME] = "ETIME",
#endif
	[ETIMEDOUT] = "ETIMEDOUT",
	[ETXTBSY] = "ETXTBSY",
	[EXDEV] = "EXDEV"
};
#endif

/*
 *	Explicitly cleanup the memory allocated to the error buffer,
 *	just in case valgrind complains about it.
 */
static void _fr_logging_free(void *arg)
{
	char *buff = talloc_get_type_abort(arg, char);
	talloc_free(buff);
}

/** Log to thread local error buffer
 *
 * @param fmt printf style format string. If NULL sets the 'new' byte to false,
 *	  effectively clearing the last message.
 */
void fr_strerror_printf(char const *fmt, ...)
{
	va_list ap;

	char *buffer;

	buffer = fr_strerror_buffer;
	if (!buffer) {
		buffer = talloc_zero_array(NULL, char, (FR_STRERROR_BUFSIZE * 2) + 1);	/* One byte extra for status */
		if (!buffer) {
			fr_perror("Failed allocating memory for libradius error buffer");
			return;
		}
		fr_thread_local_set_destructor(fr_strerror_buffer, _fr_logging_free, buffer);
	}

	/*
	 *	NULL has a special meaning, setting the new bit to false.
	 */
	if (!fmt) {
		buffer[FR_STRERROR_BUFSIZE * 2] &= 0x06;
		return;
	}

	va_start(ap, fmt);
	/*
	 *	Alternate where we write the message, so we can do:
	 *	fr_strerror_printf("Additional error: %s", fr_strerror());
	 */
	switch (buffer[FR_STRERROR_BUFSIZE * 2] & 0x06) {
	default:
		vsnprintf(buffer + FR_STRERROR_BUFSIZE, FR_STRERROR_BUFSIZE, fmt, ap);
		buffer[FR_STRERROR_BUFSIZE * 2] = 0x05;			/* Flip the 'new' bit to true */
		break;

	case 0x04:
		vsnprintf(buffer, FR_STRERROR_BUFSIZE, fmt, ap);
		buffer[FR_STRERROR_BUFSIZE * 2] = 0x03;			/* Flip the 'new' bit to true */
		break;
	}
	va_end(ap);
}

/** Get the last library error
 *
 * Will only return the last library error once, after which it will return a zero length string.
 *
 * @return library error or zero length string.
 */
char const *fr_strerror(void)
{
	char *buffer;

	buffer = fr_strerror_buffer;
	if (!buffer) return "";

	switch (buffer[FR_STRERROR_BUFSIZE * 2]) {
	default:
		return "";

	case 0x03:
		buffer[FR_STRERROR_BUFSIZE * 2] &= 0x06;		/* Flip the 'new' bit to false */
		return buffer;

	case 0x05:
		buffer[FR_STRERROR_BUFSIZE * 2] &= 0x06;		/* Flip the 'new' bit to false */
		return buffer + FR_STRERROR_BUFSIZE;
	}
}

/** Guaranteed to be thread-safe version of strerror
 *
 * @param num errno as returned by function or from global errno.
 * @return local specific error string relating to errno.
 */
char const *fr_syserror(int num)
{
	char *buffer, *p, *end;

	buffer = fr_syserror_buffer;
	if (!buffer) {
		buffer = talloc_array(NULL, char, FR_STRERROR_BUFSIZE);
		if (!buffer) {
			fr_perror("Failed allocating memory for system error buffer");
			return NULL;
		}
 		fr_thread_local_set_destructor(fr_syserror_buffer, _fr_logging_free, buffer);
	}

	if (!num) return "No error";

	p = buffer;
	end = p + FR_STRERROR_BUFSIZE;

#ifndef NDEBUG
	/*
	 *	Prefix system errors with the macro name and number
	 *	if we're debugging.
	 */
	if (num < (int)(sizeof(fr_errno_macro_names) / sizeof(*fr_errno_macro_names))) {
		p += snprintf(p, end - p, "%s: ", fr_errno_macro_names[num]);
	} else {
		p += snprintf(p, end - p, "errno %i: ", num);
	}
	if (p >= end) return p;
#endif

	/*
	 *	XSI-Compliant version
	 */
#if !defined(HAVE_FEATURES_H) || !defined(__GLIBC__) || ((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 500) && ! _GNU_SOURCE)
	{
		int ret;
		
		ret = strerror_r(num, p, end - p);
		if (ret != 0) {
#  ifndef NDEBUG
			fprintf(stderr, "strerror_r() failed to write error for errno %i to buffer %p (%zu bytes), "
				"returned %i: %s\n", num, buffer, (size_t)FR_STRERROR_BUFSIZE, ret, strerror(ret));
#  endif
			buffer[0] = '\0';
		}
	}
	return buffer;
	/*
	 *	GNU Specific version
	 *
	 *	The GNU Specific version returns a char pointer. That pointer may point
	 *	the buffer you just passed in, or to an immutable static string.
	 */
#else
	{
		p = strerror_r(num, p, end - p);
		if (!p) {
#  ifndef NDEBUG
			fprintf(stderr, "strerror_r() failed to write error for errno %i to buffer %p "
				"(%zu bytes): %s\n", num, buffer, (size_t)FR_STRERROR_BUFSIZE, strerror(errno));
#  endif
			buffer[0] = '\0';
			return buffer;
		}

		return p;
	}
#endif

}

void fr_perror(char const *fmt, ...)
{
	char const *error;
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);

	error = fr_strerror();
	if (error && (error[0] != '\0')) {
		fprintf(stderr, ": %s\n", error);
	} else {
		fputs("\n", stderr);
	}

	va_end(ap);
}

/** Canonicalize error strings, removing tabs, and generate spaces for error marker
 *
 * @note talloc_free must be called on the buffer returned in spaces and text
 *
 * Used to produce error messages such as this:
 @verbatim
  I'm a string with a parser # error
                             ^ Unexpected character in string
 @endverbatim
 *
 * With code resembling this:
 @code{.c}
   ERROR("%s", parsed_str);
   ERROR("%s^ %s", space, text);
 @endcode
 *
 * @todo merge with above function (radlog_request_marker)
 *
 * @param sp Where to write a dynamically allocated buffer of spaces used to indent the error text.
 * @param text Where to write the canonicalized version of msg (the error text).
 * @param ctx to allocate the spaces and text buffers in.
 * @param slen of error marker. Expects negative integer value, as returned by parse functions.
 * @param msg to canonicalize.
 */
void fr_canonicalize_error(TALLOC_CTX *ctx, char **sp, char **text, ssize_t slen, char const *msg)
{
	size_t offset, skip = 0;
	char *spbuf, *p;
	char *value;

	offset = -slen;

	/*
	 *	Ensure that the error isn't indented
	 *	too far.
	 */
	if (offset > 45) {
		skip = offset - 40;
		offset -= skip;
		value = talloc_strdup(ctx, msg + skip);
		memcpy(value, "...", 3);

	} else {
		value = talloc_strdup(ctx, msg);
	}

	spbuf = talloc_array(ctx, char, offset + 1);
	memset(spbuf, ' ', offset);
	spbuf[offset] = '\0';

	/*
	 *	Smash tabs to spaces for the input string.
	 */
	for (p = value; *p != '\0'; p++) {
		if (*p == '\t') *p = ' ';
	}


	/*
	 *	Ensure that there isn't too much text after the error.
	 */
	if (strlen(value) > 100) {
		memcpy(value + 95, "... ", 5);
	}

	*sp = spbuf;
	*text = value;
}

/** Maps log categories to message prefixes
 */
const FR_NAME_NUMBER fr_log_levels[] = {
	{ "Debug : ",		L_DBG		},
	{ "Auth  : ",		L_AUTH		},
	{ "Proxy : ",		L_PROXY		},
	{ "Info  : ",		L_INFO		},
	{ "Warn  : ",		L_WARN		},
	{ "Acct  : ",		L_ACCT		},
	{ "Error : ",		L_ERR		},
	{ "WARN  : ",		L_DBG_WARN	},
	{ "ERROR : ",		L_DBG_ERR	},
	{ "WARN  : ",		L_DBG_WARN_REQ	},
	{ "ERROR : ",		L_DBG_ERR_REQ	},
	{ NULL, 0 }
};

/** @name VT100 escape sequences
 *
 * These sequences may be written to VT100 terminals to change the
 * colour and style of the text.
 *
 @code{.c}
   fprintf(stdout, VTC_RED "This text will be coloured red" VTC_RESET);
 @endcode
 * @{
 */
#define VTC_RED		"\x1b[31m"	//!< Colour following text red.
#define VTC_YELLOW      "\x1b[33m"	//!< Colour following text yellow.
#define VTC_BOLD	"\x1b[1m"	//!< Embolden following text.
#define VTC_RESET	"\x1b[0m"	//!< Reset terminal text to default style/colour.
/** @} */

/** Maps log categories to VT100 style/colour escape sequences
 */
static const FR_NAME_NUMBER colours[] = {
	{ "",			L_DBG		},
	{ VTC_BOLD,		L_AUTH		},
	{ VTC_BOLD,		L_PROXY		},
	{ VTC_BOLD,		L_INFO		},
	{ VTC_BOLD,		L_ACCT		},
	{ VTC_RED,		L_ERR		},
	{ VTC_BOLD VTC_YELLOW,	L_WARN		},
	{ VTC_BOLD VTC_RED,	L_DBG_ERR	},
	{ VTC_BOLD VTC_YELLOW,	L_DBG_WARN	},
	{ VTC_BOLD VTC_RED,	L_DBG_ERR_REQ	},
	{ VTC_BOLD VTC_YELLOW,	L_DBG_WARN_REQ	},
	{ NULL, 0 }
};


bool log_dates_utc = false;

fr_log_t default_log = {
	.colourise = false,		//!< Will be set later. Should be off before we do terminal detection.
	.fd = STDOUT_FILENO,
	.dst = L_DST_STDOUT,
	.file = NULL,
	.timestamp = L_TIMESTAMP_AUTO
};

/** Send a server log message to its destination
 *
 * @param log	destination.
 * @param type	of log message.
 * @param msg	with printf style substitution tokens.
 * @param ap	Substitution arguments.
 */
int fr_vlog(fr_log_t const *log, log_type_t type, char const *msg, va_list ap)
{
	uint8_t		*p;
	char		buffer[10240];	/* The largest config item size, then extra for prefixes and suffixes */
	char		*unsan;
	size_t		len;
	int		colourise = log->colourise;

	/*
	 *	If we don't want any messages, then
	 *	throw them away.
	 */
	if (log->dst == L_DST_NULL) return 0;

	buffer[0] = '\0';
	len = 0;

	/*
	 *	Set colourisation
	 */
	if (colourise) {
		len += strlcpy(buffer + len, fr_int2str(colours, type, ""), sizeof(buffer) - len) ;
		if (len == 0) {
			colourise = false;
		}
	}

	/*
	 *	Mark the point where we treat the buffer as unsanitized.
	 */
	unsan = buffer + len;

	/*
	 *	Determine if we need to add a timestamp to the start of the message
	 */
	switch (log->timestamp) {
	case L_TIMESTAMP_OFF:
		break;

	/*
	 *	If we're not logging to syslog, and the debug level is -xxx
	 *	then log timestamps by default.
	 */
	case L_TIMESTAMP_AUTO:
		if (log->dst == L_DST_SYSLOG) break;
		if ((log->dst != L_DST_FILES) && (fr_debug_lvl <= L_DBG_LVL_2)) break;
		/* FALL-THROUGH */

	case L_TIMESTAMP_ON:
	{
		time_t timeval;

		timeval = time(NULL);
#ifdef HAVE_GMTIME_R
		if (log_dates_utc) {
			struct tm utc;
			gmtime_r(&timeval, &utc);
			ASCTIME_R(&utc, buffer + len, sizeof(buffer) - len - 1);
		} else
#endif
		{
			CTIME_R(&timeval, buffer + len, sizeof(buffer) - len - 1);
		}
		len = strlen(buffer);
		len += strlcpy(buffer + len, ": ", sizeof(buffer) - len - 1);
	}
		break;
	}

	/*
	 *	Add ERROR or WARNING prefixes to messages not going to
	 *	syslog.  It's redundant for syslog because of syslog
	 *	facilities.
	 */
	if (log->dst != L_DST_SYSLOG) {
		/*
		 *	Only print the 'facility' if we're not colourising the log messages
		 *	and this isn't syslog.
		 */
		if (!log->colourise) {
			len += strlcpy(buffer + len, fr_int2str(fr_log_levels, type, ": "), sizeof(buffer) - len);
		}

		/*
		 *	Add an additional prefix to highlight that this is a bad message
		 *	the user should pay attention to.
		 */
		if (len < sizeof(buffer)) switch (type) {
		case L_DBG_WARN:
			len += strlcpy(buffer + len, "WARNING: ", sizeof(buffer) - len);
			break;

		case L_DBG_ERR:
			len += strlcpy(buffer + len, "ERROR: ", sizeof(buffer) - len);
			break;

		default:
			break;
		}
	}

	if (len < sizeof(buffer)) len += vsnprintf(buffer + len, sizeof(buffer) - len - 1, msg, ap);

	/*
	 *	Filter out control chars and non UTF8 chars
	 */
	for (p = (unsigned char *)unsan; *p != '\0'; p++) {
		int clen;

		switch (*p) {
		case '\r':
		case '\n':
			*p = ' ';
			break;

		case '\t':
			continue;

		default:
			clen = fr_utf8_char(p, -1);
			if (!clen) {
				*p = '?';
				continue;
			}
			p += (clen - 1);
			break;
		}
	}

	/*
	 *	Reset colourisation if we applied it
	 */
	if (colourise && (len < sizeof(buffer))) {
		len += strlcpy(buffer + len, VTC_RESET, sizeof(buffer) - len);
	}

	if (len < (sizeof(buffer) - 2)) {
		buffer[len]	= '\n';
		buffer[len + 1] = '\0';
	} else {
		buffer[sizeof(buffer) - 2] = '\n';
		buffer[sizeof(buffer) - 1] = '\0';
	}

	switch (log->dst) {

#ifdef HAVE_SYSLOG_H
	case L_DST_SYSLOG:
		switch (type) {
		case L_DBG:
		case L_DBG_INFO:
		case L_DBG_WARN:
		case L_DBG_ERR:
		case L_DBG_ERR_REQ:
		case L_DBG_WARN_REQ:
			type = LOG_DEBUG;
			break;

		case L_AUTH:
		case L_PROXY:
		case L_ACCT:
			type = LOG_NOTICE;
			break;

		case L_INFO:
			type = LOG_INFO;
			break;

		case L_WARN:
			type = LOG_WARNING;
			break;

		case L_ERR:
			type = LOG_ERR;
			break;
		}
		syslog(type, "%s", buffer);
		break;
#endif

	case L_DST_FILES:
	case L_DST_STDOUT:
	case L_DST_STDERR:
		return write(log->fd, buffer, strlen(buffer));

	default:
	case L_DST_NULL:	/* should have been caught above */
		break;
	}

	return 0;
}

/** Send a server log message to its destination
 *
 * @param log	destination.
 * @param type	of log message.
 * @param msg	with printf style substitution tokens.
 * @param ...	Substitution arguments.
 */
int fr_log(fr_log_t const *log, log_type_t type, char const *msg, ...)
{
	va_list ap;
	int r = 0;

	va_start(ap, msg);

	/*
	 *	Non-debug message, or debugging is enabled.  Log it.
	 */
	if (((type & L_DBG) == 0) || (fr_debug_lvl > 0)) {
		r = fr_vlog(log, type, msg, ap);
	}
	va_end(ap);

	return r;
}
