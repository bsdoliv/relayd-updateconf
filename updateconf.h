/*
 * Copyright (c) 2014 Andre de Oliveira <deoliveirambx@googlemail.com>
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

#ifndef _UPDATECONF_H
#define _UPDATECONF_H

/* updateconf.c */
__dead void	  fatal(const char *);
char		**opts_add(char **argv, size_t *argc, const char *s);
char		 *getsslflag(u_int8_t);
void		 rts_print_forward(struct router *, char *, size_t);
void		 rts_print_route(struct netroute *, char *, size_t);
void		 table_print(struct table *, char *, size_t);
char		*print_tcpport(in_port_t);
void		 proto_print_node(struct protocol *, struct protonode *,
		    char **);
void		 proto_print_opts(struct protocol *, char *, size_t);
void		 proto_print_flags(struct protocol *, char *, size_t);
void		 proto_print_label(const char *, char *, size_t);
void		 rdr_print_forward(struct rdr *, int, char *, size_t);
void		 rdr_print_listen(struct rdr *, char *, size_t);
void		 relay_print_forward(struct relay *, struct relay_table *,
		     char *, size_t);
void		 relay_print_listen(struct relay *, char *, size_t);
int		 protonode_add(enum direction, struct protocol *,
		    struct protonode *);

#endif /* _UPDATECONF_H */
