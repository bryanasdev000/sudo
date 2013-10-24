/*
 * Copyright (c) 2013 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>
#ifdef HAVE_SYS_SYSMACROS_H
# include <sys/sysmacros.h>
#endif
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# include "compat/stdbool.h"
#endif /* HAVE_STDBOOL_H */
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <errno.h>

#include "missing.h"
#include "alloc.h"
#include "fatal.h"
#include "sudo_debug.h"
#include "sudo_event.h"

/* XXX - use non-exiting allocators? */

int
sudo_ev_base_alloc_impl(struct sudo_event_base *base)
{
    debug_decl(sudo_ev_base_alloc_impl, SUDO_DEBUG_EVENT)

    base->maxfd = NFDBITS - 1;
    base->nevents = 0;
    base->readfds = ecalloc(1, sizeof(fd_mask));
    base->writefds = ecalloc(1, sizeof(fd_mask));

    debug_return_int(0);
}

void
sudo_ev_base_free_impl(struct sudo_event_base *base)
{
    debug_decl(sudo_ev_base_free_impl, SUDO_DEBUG_EVENT)
    efree(base->readfds);
    efree(base->writefds);
    debug_return;
}

int
sudo_ev_add_impl(struct sudo_event_base *base, struct sudo_event *ev)
{
    debug_decl(sudo_ev_add_impl, SUDO_DEBUG_EVENT)

    /* If out of space in fd sets, realloc. */
    if (ev->fd > base->maxfd) {
	const int n = howmany(ev->fd + 1, NFDBITS);
	efree(base->readfds);
	efree(base->writefds);
	base->readfds = ecalloc(n, sizeof(fd_mask));
	base->writefds = ecalloc(n, sizeof(fd_mask));
	base->maxfd = (n * NFDBITS) - 1;
    }
    base->nevents++;

    debug_return_int(0);
}

int
sudo_ev_del_impl(struct sudo_event_base *base, struct sudo_event *ev)
{
    debug_decl(sudo_ev_del_impl, SUDO_DEBUG_EVENT)

    /* Remove from readfds and writefds and adjust event count. */
    FD_CLR(ev->fd, base->readfds);
    FD_CLR(ev->fd, base->writefds);
    base->nevents--;

    debug_return_int(0);
}

int
sudo_ev_loop_impl(struct sudo_event_base *base, int flags)
{
    struct timeval tv, *timeout;
    struct sudo_event *ev;
    int nready;
    debug_decl(sudo_ev_loop, SUDO_DEBUG_EVENT)

    if (ISSET(flags, SUDO_EVLOOP_NONBLOCK)) {
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	timeout = &tv;
    } else {
	timeout = NULL;
    }

rescan:
    while (base->nevents != 0) {
	int highfd = 0;
	/* For select we need to redo readfds and writefds each time. */
	memset(base->readfds, 0, howmany(base->maxfd + 1, NFDBITS) * sizeof(fd_mask));
	memset(base->writefds, 0, howmany(base->maxfd + 1, NFDBITS) * sizeof(fd_mask));
	TAILQ_FOREACH(ev, &base->events, entries) {
	    if (ISSET(ev->events, SUDO_EV_READ)) {
		sudo_debug_printf(SUDO_DEBUG_DEBUG, "%s: added fd %d to readfs",
		    __func__, ev->fd);
		FD_SET(ev->fd, base->readfds);
		if (ev->fd > highfd)
		    highfd = ev->fd;
	    }
	    if (ISSET(ev->events, SUDO_EV_WRITE)) {
		sudo_debug_printf(SUDO_DEBUG_DEBUG, "%s: added fd %d to writefds",
		    __func__, ev->fd);
		FD_SET(ev->fd, base->writefds);
		if (ev->fd > highfd)
		    highfd = ev->fd;
	    }
	}
	sudo_debug_printf(SUDO_DEBUG_DEBUG, "%s: select high fd %d",
	    __func__, highfd);
	nready = select(highfd + 1, base->readfds, base->writefds, NULL, timeout);
	sudo_debug_printf(SUDO_DEBUG_INFO, "%s: %d fds ready",
	    __func__, nready);
	switch (nready) {
	case -1:
	    if (errno == EINTR || errno == ENOMEM)
		continue;
	    debug_return_int(-1);
	case 0:
	    /* timeout or no events */
	    break;
	default:
	    /* Service each event that fired. */
	    TAILQ_FOREACH_SAFE(ev, &base->events, entries, base->pending) {
		int what = 0;
		if (FD_ISSET(ev->fd, base->readfds))
		    what |= (ev->events & SUDO_EV_READ);
		if (FD_ISSET(ev->fd, base->writefds))
		    what |= (ev->events & SUDO_EV_WRITE);
		if (what != 0) {
		    if (!ISSET(ev->events, SUDO_EV_PERSIST))
			SET(ev->events, SUDO_EV_DELETE);
		    ev->callback(ev->fd, what, ev->closure);
		    if (ISSET(ev->events, SUDO_EV_DELETE))
			sudo_ev_del(base, ev);
		    if (ISSET(base->flags, SUDO_EVBASE_LOOPBREAK)) {
			/* stop processing events immediately */
			base->flags |= SUDO_EVBASE_GOT_BREAK;
			base->pending = NULL;
			goto done;
		    }
		    if (ISSET(base->flags, SUDO_EVBASE_LOOPCONT)) {
			/* rescan events and start polling again */
			CLR(base->flags, SUDO_EVBASE_LOOPCONT);
			base->pending = NULL;
			goto rescan;
		    }
		}
	    }
	    base->pending = NULL;
	    if (ISSET(base->flags, SUDO_EVBASE_LOOPEXIT)) {
		/* exit loop after once through */
		base->flags |= SUDO_EVBASE_GOT_EXIT;
		goto done;
	    }
	    break;
	}
	if (flags & (SUDO_EVLOOP_ONCE | SUDO_EVLOOP_NONBLOCK))
	    break;
    }
done:
    debug_return_int(0);
}