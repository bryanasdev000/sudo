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

#ifndef _SUDO_EVENT_H
#define _SUDO_EVENT_H

#include "queue.h"

/* Event types */
#define SUDO_EV_READ	0x01		/* fire when readable */
#define SUDO_EV_WRITE	0x02		/* fire when writable */
#define SUDO_EV_PERSIST	0x04		/* persist until deleted */
#define SUDO_EV_DELETE	0x08		/* deletion pending */

/* Event loop flags */
#define SUDO_EVLOOP_ONCE	0x01	/* Only run once through the loop */
#define SUDO_EVLOOP_NONBLOCK	0x02	/* Do not block in event loop */

/* Event base flags (internal) */
#define SUDO_EVBASE_LOOPEXIT	0x01
#define SUDO_EVBASE_LOOPBREAK	0x02
#define SUDO_EVBASE_LOOPCONT	0x04
#define SUDO_EVBASE_GOT_EXIT	0x10
#define SUDO_EVBASE_GOT_BREAK	0x20
#define SUDO_EVBASE_GOT_MASK	0xf0

typedef void (*sudo_ev_callback_t)(int fd, int what, void *closure);

/* Member of struct sudo_event_base. */
struct sudo_event {
    TAILQ_ENTRY(sudo_event) entries;
    struct sudo_event_base *base; /* base this event belongs to */
    int fd;			/* fd we are interested in */
    short events;		/* SUDO_EV_* flags */
    short pfd_idx;		/* index into pfds array */
    sudo_ev_callback_t callback;/* user-provided callback */
    void *closure;		/* user-provided data pointer */
};

TAILQ_HEAD(sudo_event_list, sudo_event);

struct sudo_event_base {
    struct sudo_event_list events; /* tail queue of events */
    /* XXX - also list of active events and timed events */
    struct sudo_event *pending;	/* next event to be run in the event loop XXX */
#ifdef HAVE_POLL
    struct pollfd *pfds;	/* array of struct pollfd */
    int pfd_max;		/* size of the pfds array */
    int pfd_high;		/* highest slot used */
    int pfd_free;		/* idx of next free entry or pfd_max if full */
#else
    fd_set *readfds;		/* read I/O descriptor set */
    fd_set *writefds;		/* write I/O descriptor set */
    int maxfd;			/* max fd we can store in readfds/writefds */
    int nevents;		/* number of events in the list */
#endif /* HAVE_POLL */
    unsigned int flags;		/* SUDO_EVBASE_* */
};

/* Allocate a new event base. */
struct sudo_event_base *sudo_ev_base_alloc(void);

/* Free an event base. */
void sudo_ev_base_free(struct sudo_event_base *base);

/* Allocate a new event. */
struct sudo_event *sudo_ev_alloc(int fd, short events, sudo_ev_callback_t callback, void *closure);

/* Free an event. */
void sudo_ev_free(struct sudo_event *ev);

/* Add an event, returns 0 on success, -1 on error */
int sudo_ev_add(struct sudo_event_base *head, struct sudo_event *ev, bool tohead);

/* Delete an event, returns 0 on success, -1 on error */
int sudo_ev_del(struct sudo_event_base *head, struct sudo_event *ev);

/* Main event loop, returns SUDO_CB_SUCCESS, SUDO_CB_BREAK or SUDO_CB_ERROR */
int sudo_ev_loop(struct sudo_event_base *head, int flags);

/* Cause the event loop to exit after one run through. */
void sudo_ev_loopexit(struct sudo_event_base *base);

/* Break out of the event loop right now. */
void sudo_ev_loopbreak(struct sudo_event_base *base);

/* Rescan for events and restart the event loop. */
void sudo_ev_loopcontinue(struct sudo_event_base *base);

/* Returns true if event loop stopped due to sudo_ev_loopexit(). */
bool sudo_ev_got_exit(struct sudo_event_base *base);

/* Returns true if event loop stopped due to sudo_ev_loopbreak(). */
bool sudo_ev_got_break(struct sudo_event_base *base);

/* Return the fd associated with an event. */
#define sudo_ev_get_fd(_ev) ((_ev) ? (_ev)->fd : -1)

/* Return the base an event is associated with or NULL. */
#define sudo_ev_get_base(_ev) ((_ev) ? (_ev)->base : NULL)

/*
 * Backend implementation.
 */
int sudo_ev_base_alloc_impl(struct sudo_event_base *base);
void sudo_ev_base_free_impl(struct sudo_event_base *base);
int sudo_ev_add_impl(struct sudo_event_base *base, struct sudo_event *ev);
int sudo_ev_del_impl(struct sudo_event_base *base, struct sudo_event *ev);
int sudo_ev_loop_impl(struct sudo_event_base *base, int flags);

#endif /* _SUDO_EVENT_H */