/*
 * Copyright (c) 2014 Andre de Oliveira <andre@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include "relayd.h"
#include "updateconf.h"

__dead void	 usage(void);
void		 parseconf(struct relayd *env);
char		*getprototype(struct protocol *);
char		*getdstmode(enum dstmode);
const char	*print_host(struct sockaddr_storage *, char *, size_t);
char		*print_tablecheck(struct table *);
char		*print_tcpport(in_port_t);
void		 print_rts_forward(struct router *);
void		 protonode_opts_add(struct protocol *, struct protonode *);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-f file]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int			 c;
	const char		*conffile = "/etc/relayd.conf";
	struct relayd		*env;

	while ((c = getopt(argc, argv, "f:")) != -1) {
		switch (c) {
		case 'f':
			conffile = optarg;
			break;
		default:
			usage();
		}
	}

	if ((env = calloc(1, sizeof(*env))) == NULL)
		errx(1, "out of memory");

	env->sc_conffile = conffile;
	env->sc_timeout.tv_usec = (CHECK_TIMEOUT % 1000) * 1000;
	env->sc_interval.tv_sec = CHECK_INTERVAL;
	env->sc_interval.tv_usec = 0;

	if ((env->sc_tables =
	    calloc(1, sizeof(*env->sc_tables))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_tables);

	memset(&env->sc_empty_table, 0, sizeof(env->sc_empty_table));
	env->sc_empty_table.conf.id = EMPTY_TABLE;
	env->sc_empty_table.conf.flags |= F_DISABLE;
	(void)strlcpy(env->sc_empty_table.conf.name, "empty",
	    sizeof(env->sc_empty_table.conf.name));

	if ((env->sc_rdrs =
	    calloc(1, sizeof(*env->sc_rdrs))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_rdrs);

	if ((env->sc_relays = calloc(1, sizeof(*env->sc_relays))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_relays);

	if ((env->sc_protos =
	    calloc(1, sizeof(*env->sc_protos))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_protos);

	bzero(&env->sc_proto_default, sizeof(env->sc_proto_default));
	env->sc_proto_default.id = EMPTY_ID;
	env->sc_proto_default.flags = F_USED;
	env->sc_proto_default.cache = RELAY_CACHESIZE;
	env->sc_proto_default.tcpflags = TCPFLAG_DEFAULT;
	env->sc_proto_default.tcpbacklog = RELAY_BACKLOG;
	env->sc_proto_default.sslflags = SSLFLAG_DEFAULT;
	(void)strlcpy(env->sc_proto_default.sslciphers,
	    SSLCIPHERS_DEFAULT,
	    sizeof(env->sc_proto_default.sslciphers));
	env->sc_proto_default.type = RELAY_PROTO_TCP;
	(void)strlcpy(env->sc_proto_default.name, "default",
	    sizeof(env->sc_proto_default.name));

	if ((env->sc_rts = calloc(1, sizeof(*env->sc_rts))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_rts);

	if ((env->sc_routes = calloc(1, sizeof(*env->sc_routes))) == NULL)
		errx(-1, "cannot initialize configuration");
	TAILQ_INIT(env->sc_routes);

	if (load_config(env->sc_conffile, env) == -1)
		errx(1, "fail on load_config()");

	parseconf(env);
	return (0);
}

void
parseconf(struct relayd *env)
{
	struct table		*table;
	struct rdr		*rdr;
	struct address		*virt;
	struct protocol		*proto;
	struct relay		*rlay;
	struct netroute		*nr;
	struct router		*rt;
	size_t			 i = 0;

	if (env->sc_mainoptsc > 0) {
		for (i = 0; i < env->sc_mainoptsc; i++) {
			fprintf(stdout, "%s\n", env->sc_mainopts[i]);
			free(env->sc_mainopts[i]);
			env->sc_mainopts[i] = NULL;
		}
		free(env->sc_mainopts);
		env->sc_mainoptsc = 0;
	}
	if (env->sc_tableoptsc > 0) {
		/* print tables */
		for (i = 0; i < (unsigned int)env->sc_tableoptsc; i++) {
			fprintf(stdout, "%s\n", env->sc_tableopts[i]);
			free(env->sc_tableopts[i]);
			env->sc_tableopts[i] = NULL;
		}
		free(env->sc_tableopts);
		env->sc_tableoptsc = 0;
	}
	if (env->sc_protos != NULL) {
		while ((proto = TAILQ_FIRST(env->sc_protos)) != NULL) {
			TAILQ_REMOVE(env->sc_protos, proto, entry);

			fprintf(stdout, "%s%sprotocol \"%s\" {\n",
			    getprototype(proto) == NULL ? "" :
			    getprototype(proto),
			    getprototype(proto) == NULL ? "" : " ",
			    proto->name);
			/* print protocol opts */
			if (proto->optsc > 0) {
				for (i = 0; i < proto->optsc; i++) {
					fprintf(stdout, "\t%s\n",
					    proto->opts[i]);
					free(proto->opts[i]);
					proto->opts[i] = NULL;
				}
				free(proto->opts);
				proto->optsc = 0;
			}
			/* print rules */
			if (proto->rulesc > 0) {
				for (i = 0; i < proto->rulesc; i++) {
					fprintf(stdout, "\t%s\n",
					    proto->rules[i]);
					free(proto->rules[i]);
					proto->rules[i] = NULL;
				}
				free(proto->rules);
				proto->rulesc = 0;
			}
			fprintf(stdout, "}\n");

			if (proto->style != NULL)
				free(proto->style);
			if (proto->sslcapass != NULL)
				free(proto->sslcapass);
			free(proto);
		}
		env->sc_protocount = 0;
	}
	if (env->sc_rts != NULL) {
		while ((rt = TAILQ_FIRST(env->sc_rts)) != NULL) {
			TAILQ_REMOVE(env->sc_rts, rt, rt_entry);
			/* print routers */
			fprintf(stdout, "router \"%s\" {\n", rt->rt_conf.name);
			while ((nr = TAILQ_FIRST(&rt->rt_netroutes)) != NULL) {
				TAILQ_REMOVE(&rt->rt_netroutes, nr, nr_entry);
				TAILQ_REMOVE(env->sc_routes, nr, nr_route);
				free(nr);
				env->sc_routecount--;
			}
			/* print router opts */
			if (rt->optsc > 0) {
				for (i = 0; i < rt->optsc; i++) {
					fprintf(stdout, "\t%s\n",
						rt->opts[i]);
					free(rt->opts[i]);
					rt->opts[i] = NULL;
				}
				free(rt->opts);
				rt->optsc = 0;
			}
			fprintf(stdout, "}\n");
			free(rt);
		}
		env->sc_routercount = 0;
	}
	if (env->sc_rdrs != NULL) {
		while ((rdr = TAILQ_FIRST(env->sc_rdrs)) != NULL) {
			TAILQ_REMOVE(env->sc_rdrs, rdr, entry);
			/* print redirects */
			fprintf(stdout, "redirect \"%s\" {\n", rdr->conf.name);
			if (rdr->optsc > 0) {
				for (i = 0; i < rdr->optsc; i++) {
					fprintf(stdout, "\t%s\n",
					    rdr->opts[i]);
					free(rdr->opts[i]);
					rdr->opts[i] = NULL;
				}
				free(rdr->opts);
				rdr->optsc = 0;
			}
			fprintf(stdout, "}\n");

			while ((virt = TAILQ_FIRST(&rdr->virts)) != NULL) {
				TAILQ_REMOVE(&rdr->virts, virt, entry);
				free(virt);
			}
			free(rdr);
		}
		env->sc_rdrcount = 0;
	}
	if (env->sc_relays != NULL) {
		while ((rlay = TAILQ_FIRST(env->sc_relays)) != NULL) {
			/* print relays */
			fprintf(stdout, "relay \"%s\" {\n", rlay->rl_conf.name);
			if (rlay->rl_optsc > 0) {
				for (i = 0; i < rlay->rl_optsc; i++) {
					fprintf(stdout, "\t%s\n",
					    rlay->rl_opts[i]);
					free(rlay->rl_opts[i]);
					rlay->rl_opts[i] = NULL;
				}
				free(rlay->rl_opts);
				rlay->rl_optsc = 0;
			}
			fprintf(stdout, "}\n");

			purge_relay(env, rlay);
		}
		env->sc_relaycount = 0;
	}
	if (env->sc_routes != NULL) {
		while ((nr = TAILQ_FIRST(env->sc_routes)) != NULL) {
			if ((rt = nr->nr_router) != NULL)
				TAILQ_REMOVE(&rt->rt_netroutes, nr, nr_entry);
			TAILQ_REMOVE(env->sc_routes, nr, nr_route);
			free(nr);
		}
		env->sc_routecount = 0;
	}
	if (env->sc_tables != NULL) {
		while ((table = TAILQ_FIRST(env->sc_tables)) != NULL)
			purge_table(env->sc_tables, table);
		env->sc_tablecount = 0;
	}
}

int
protonode_add(enum direction dir, struct protocol *proto,
    struct protonode *node)
{
	if (dir == RELAY_DIR_RESPONSE)
		node->id = proto->response_nodes++;
	else
		node->id = proto->request_nodes++;
	if (node->id == INT_MAX) {
		warnx("%s: too many protocol nodes defined", __func__);
		return (-1);
	}
	protonode_opts_add(proto, node);

	if (node->labelname)
		free(node->labelname);
	if (node->key)
		free(node->key);
	if (node->value)
		free(node->value);
	node->key = node->value = node->labelname = NULL;
	return (0);
}

char *
getdstmode(enum dstmode m)
{
	switch (m) {
	case RELAY_DSTMODE_LOADBALANCE:
		return ("mode loadbalance");
		break;
	case RELAY_DSTMODE_HASH:
		return ("mode hash");
		break;
	case RELAY_DSTMODE_SRCHASH:
		return ("mode srchash");
		break;
	case RELAY_DSTMODE_RANDOM:
		return ("mode random");
		break;
	case RELAY_DSTMODE_LEASTSTATES:
		return ("mode least-states");
		break;
	case RELAY_DSTMODE_DEFAULT:
	default:
		return ("");
		break;
	}
}

char *
print_tcpport(in_port_t p)
{
	char *buf = NULL;

	if (p <= 0 || p >= (int)USHRT_MAX)
		return (NULL);

	if (asprintf(&buf, "%u", ntohs(p)) == -1)
		return (NULL);

	return (buf);
}

char *
print_tablecheck(struct table *t)
{
	int	 s = 0;
	char	*p;
	char	 buf[256];

	bzero(buf, sizeof(buf));
	switch (t->conf.check) {
	case CHECK_ICMP:
		snprintf(buf, sizeof(buf), "icmp");
		break;
	case CHECK_TCP:
		if (t->conf.flags & F_SSL)
			snprintf(buf, sizeof(buf), "ssl");
		else
			snprintf(buf, sizeof(buf), "tcp");
		break;
	case CHECK_HTTP_CODE:
		if (t->conf.flags & F_SSL)
			s = snprintf(buf, sizeof(buf), "https ");
		else
			s = snprintf(buf, sizeof(buf), "http ");

		s = snprintf(buf + s, sizeof(buf), "\"%s\" code %d",
		    t->sendbuf, t->conf.retcode);
		break;
	case CHECK_HTTP_DIGEST:
		if (t->conf.flags & F_SSL)
			s = snprintf(buf, sizeof(buf), "https ");
		else
			s = snprintf(buf, sizeof(buf), "http ");
		s = snprintf(buf + s, sizeof(buf), "\"%s\" digest %s",
		    t->sendbuf, t->conf.digest);
		break;
	case CHECK_SEND_EXPECT:
		snprintf(buf, sizeof(buf), "send");
		break;
	case CHECK_SCRIPT:
		snprintf(buf, sizeof(buf), "script");
		break;
	default:
		break;
	}
	if (asprintf(&p, "check %s", buf) == -1)
		fatal("out of memory");

	return (p);
}

void
rts_print_forward(struct router *rt, char *buf, size_t buflen)
{
	char	*port = NULL;
	char	*check = NULL;
	int	 lenopt = 0;

	rt->rt_gwtable->conf.name[strcspn(rt->rt_gwtable->conf.name, ":")] =
	    '\0';
	port = print_tcpport(rt->rt_conf.gwport);
	lenopt += snprintf(buf, buflen, "forward to <%s>%s%s",
	    rt->rt_gwtable->conf.name,
	    port == NULL ? "" : " port ",
	    port == NULL ? "" : port);

	if (port)
		free(port);

	if ((check = print_tablecheck(rt->rt_gwtable)) != NULL) {
		(void)snprintf(buf + lenopt, buflen, " %s", check);
		free(check);
	}
}

void
rts_print_route(struct netroute *nr, char *buf, size_t buflen)
{
	char	 host[MAXHOSTNAMELEN];

	bzero(host, sizeof(host));
	print_host(&nr->nr_conf.ss, host, sizeof(host));
	if (host[0])
		snprintf(buf, buflen, "route %s/%d", host,
		    nr->nr_conf.prefixlen);
}

void
relay_print_forward(struct relay *rlay, struct relay_table *rlt, char *buf,
    size_t len)
{
	char	*check = NULL;
	char	*port = NULL;
	char	 hsttbl[MAXHOSTNAMELEN];
	char	 tmptbl[MAXHOSTNAMELEN];
	char	 opts[256];
	int	 lenopt = 0;

	bzero(opts, sizeof(opts));
	bzero(hsttbl, sizeof(hsttbl));
	bzero(tmptbl, sizeof(tmptbl));

	/* destination host/tablename */
	if (rlay->rl_conf.flags & F_DIVERT && rlt == NULL)
		(void)snprintf(hsttbl, sizeof(hsttbl), "destination");
	else if (rlay->rl_conf.flags & F_NATLOOK && rlt == NULL)
		(void)snprintf(hsttbl, sizeof(hsttbl), "nat lookup");
	else if (rlay->rl_conf.dstss.ss_family != AF_UNSPEC && rlt == NULL) {
		print_host(&rlay->rl_conf.dstss, hsttbl, sizeof(hsttbl));
		port = print_tcpport(rlay->rl_conf.dstport);
	} else if (rlt) {
		/* table stuff: destination mode, check */
		(void)strlcpy(tmptbl, rlt->rlt_table->conf.name,
		    sizeof(tmptbl));
		tmptbl[strcspn(tmptbl, ":")] = '\0';
		(void)snprintf(hsttbl, sizeof(hsttbl), "<%s>", tmptbl);
		lenopt += snprintf(opts + lenopt, sizeof(opts), " %s",
		    getdstmode(rlt->rlt_mode));
		check = print_tablecheck(rlt->rlt_table);
		if (check) {
			lenopt += snprintf(opts + lenopt, sizeof(opts), " %s",
			    check);
			free(check);
		}
		port = print_tcpport(rlt->rlt_table->conf.port);
	}

	/* opts */
	if (rlay->rl_conf.dstretry > 0)
		lenopt += snprintf(opts + lenopt, sizeof(opts), "retry %d",
		    rlay->rl_conf.dstretry);
	switch (rlay->rl_conf.dstaf.ss_family) {
	case AF_INET:
		lenopt += snprintf(opts + lenopt, sizeof(opts),
		    " inet");
		break;
	case AF_INET6:
		lenopt += snprintf(opts + lenopt, sizeof(opts),
		    " inet6 %s", hsttbl);
		break;
	}

	/* forward to address port port options */
	(void)snprintf(buf, len, "%s%s%s to%s%s%s%s%s%s%s",
	    (rlay->rl_conf.fwdmode == FWD_TRANS) ? "transparent " : "",
	    (rlay->rl_conf.fwdmode == FWD_ROUTE) ? "route" : "forward",
	    (rlay->rl_conf.flags & F_SSLCLIENT) ? " with ssl" : "",
	    !hsttbl[0] ? "" : " ",
	    !hsttbl[0] ? "" : hsttbl,
	    port == NULL ? "" : " port ",
	    port == NULL ? "" : port,
	    !rlay->rl_conf.ifname[0] ? "" : " interface ",
	    !rlay->rl_conf.ifname[0] ? "" : rlay->rl_conf.ifname,
	    !opts[0] ? "" : opts);

	if (port)
		free(port);
}

void
relay_print_listen(struct relay *rlay, char *buf, size_t buflen)
{
	char *p = NULL;
	char  host[64];

	p = print_tcpport(rlay->rl_conf.port);
	print_host(&rlay->rl_conf.ss, host, sizeof(host));
	(void)snprintf(buf, buflen, "listen on %s%s%s%s",
		host,
		p == NULL ? "" : " port ",
		p == NULL ? "" : p,
		rlay->rl_conf.flags & F_SSL ? " ssl" : "");
	if (p)
		free(p);
}

char **
opts_add(char **argv, size_t *argc, const char *s)
{
	char	**p;
	char	 *entry;
	size_t	  size;

	if ((entry = strdup(s)) == NULL)
		fatal("opt_add() out of memory");
	size = (*argc + 1) * sizeof(char **);
	if ((p = realloc(argv, size)) == NULL) {
		free(entry);
		free(argv);
		fatal("opt_add() out of memory");
	}
	p[*argc] = entry;
	(*argc)++;
	return (p);
}

void
rdr_print_forward(struct rdr *r, int backup, char *buf, size_t len)
{
	enum forwardmode fwdmode;
	struct table	*t;
	char		 mode[64];
	char		 table[64];
	char		*check;

	if (backup)
		t = r->backup;
	else
		t = r->table;

	bzero(table, sizeof(table));
	bzero(mode, sizeof(mode));

	t->conf.name[strcspn(t->conf.name, ":")] = '\0';
	(void)snprintf(table, sizeof(table), "<%s>", t->conf.name);

	fwdmode = t->conf.fwdmode;
	(void)strlcpy(mode, getdstmode(r->conf.mode), sizeof(mode));
	check = print_tablecheck(t);

	/* forward to address port port options */
	snprintf(buf, len, "%s%s to%s%s%s%s%s%s%s%s",
	    (fwdmode == FWD_TRANS) ? "transparent " : "",
	    (fwdmode == FWD_ROUTE) ? "route" : "forward",
	    !table[0] ? "" : " ",
	    !table[0] ? "" : table,
	    !mode[0] ? "" : " ",
	    !mode[0] ? "" : mode,
	    !t->conf.ifname[0] ? "" : " interface ",
	    !t->conf.ifname[0] ? "" : t->conf.ifname,
	    check == NULL ? "" : " ",
	    check == NULL ? "" : check);
	if (check)
		free(check);
}

void
rdr_print_listen(struct rdr *r, char *buf, size_t len)
{
	struct protoent *pe;
	struct address	*a;
	char		*port;
	char		 host[64];

	a = TAILQ_FIRST(&r->virts);
	port = print_tcpport(r->conf.port);
	pe = getprotobynumber(a->ipproto);
	print_host(&a->ss, host, sizeof(host));
	(void)snprintf(buf, len, "listen on %s%s%s%s%s%s%s",
	    host,
	    pe == NULL ? "" : " ",
	    pe == NULL ? "" : pe->p_name,
	    port == NULL ? "" : " port ",
	    port == NULL ? "" : port,
	    !a->ifname[0] ? "" : " interface ",
	    !a->ifname[0] ? "" : a->ifname);
	if (port)
		free(port);
}

void
table_print(struct table *table, char *buf, size_t len)
{
	int		 offs = 0;
	struct host	*host;

	bzero(buf, len);
	offs += snprintf(buf, len, "table <%s> %s{", table->conf.name,
	    table->conf.flags & F_DISABLE ? "disable " : "");
	TAILQ_FOREACH(host, &table->hosts, entry) {
		offs += snprintf(buf + offs, len, " %s", host->conf.name);
		if (host->conf.retry > 0)
			offs += snprintf(buf + offs, len, " retry %d",
			    host->conf.retry);
		if (host->conf.parentid > 0)
			offs += snprintf(buf + offs, len, " parent %d",
			    host->conf.parentid);
		if (host->conf.priority > 0)
			offs += snprintf(buf + offs, len, " priority %d",
			    host->conf.priority);
		if (host->conf.ttl > 0)
			offs += snprintf(buf + offs, len, " ip ttl %d",
			    host->conf.ttl);
		offs += snprintf(buf + offs, len, ",");
	}
	/* step back from trailing comma */
	(void)snprintf(buf + (--offs), len, " }");
}

void
fatal(const char *emsg)
{
	if (emsg == NULL)
		fprintf(stderr, "fatal: %s", strerror(errno));
	else
		if (errno)
			fprintf(stderr, "fatal: %s: %s", emsg,
			    strerror(errno));
		else
			fprintf(stderr, "fatal: %s", emsg);

	fprintf(stderr, "\n");
	exit(1);
}

const char *
print_host(struct sockaddr_storage *ss, char *buf, size_t len)
{
	if (getnameinfo((struct sockaddr *)ss, ss->ss_len,
	    buf, len, NULL, 0, NI_NUMERICHOST) != 0) {
		buf[0] = '\0';
		return (NULL);
	}
	return (buf);
}

void
proto_print_opts(struct protocol *proto, char *buf, size_t len)
{
	int	off = 0;

	bzero(buf, len);
	if (proto->flags & F_RETURN) {
		(void)snprintf(buf + off, len, "return error%s%s%s",
		    proto->style == NULL ? "" : " style \"",
		    proto->style == NULL ? "" : proto->style,
		    proto->style == NULL ? "" : "\"");
	}
}

void
proto_print_label(const char *label, char *buf, size_t len)
{
	if (label == NULL)
		(void)snprintf(buf, len, "match request no label");
	else
		(void)snprintf(buf, len, "match request label \"%s\"", label);
}

void
proto_print_flags(struct protocol *proto, char *buf, size_t len)
{
	int 	off = 0, i = 0;
	char	p[64];

#define FLAGADDBUF(s)				\
	do {					\
		off += snprintf(buf + off, len,	\
		    "%s%s",			\
		    (i++ == 0) ? "" : ", ", s);	\
	} while (0)

	bzero(buf, len);
	if (proto->tcpflags != TCPFLAG_DEFAULT) {
		off += snprintf(buf, len, "tcp { ");
		if (proto->tcpflags & TCPFLAG_NODELAY)
			FLAGADDBUF("nodelay");
		if (proto->tcpflags & TCPFLAG_NNODELAY)
			FLAGADDBUF("no nodelay");
		if (proto->tcpflags & TCPFLAG_SACK)
			FLAGADDBUF("sack");
		if (proto->tcpflags & TCPFLAG_NSACK)
			FLAGADDBUF("no sack");
		if (proto->tcpflags & TCPFLAG_BUFSIZ) {
			bzero(p, sizeof(p));
			(void)snprintf(p, sizeof(p), "socket buffer %d",
			     proto->tcpbufsiz);
			FLAGADDBUF(p);
		}
		if (proto->tcpflags & TCPFLAG_IPTTL) {
			bzero(p, sizeof(p));
			(void)snprintf(p, sizeof(p), "ip ttl %d",
			     proto->tcpipttl);
			FLAGADDBUF(p);
		}
		if (proto->tcpflags & TCPFLAG_IPMINTTL) {
			bzero(p, sizeof(p));
			(void)snprintf(p, sizeof(p), "ip minttl %d",
			     proto->tcpipminttl);
			FLAGADDBUF(p);
		}
		if (proto->tcpflags & TCPFLAG_NSPLICE)
			FLAGADDBUF("no splice");

		bzero(p, sizeof(p));
		if (proto->tcpbacklog != RELAY_BACKLOG) {
			(void)snprintf(p, sizeof(p), "backlog %d",
			    proto->tcpbacklog);
			FLAGADDBUF(p);
		}

		(void)snprintf(buf + off, len, " }");
	}
}

void
protonode_opts_add(struct protocol *proto, struct protonode *pn)
{
	char	*mark = NULL;
	char	*quick = NULL;
	char	*buf = NULL;
	char	 keyaction[64];
	char	 ruleaction[64];
	char	 keytype[64];
	int	 nodeaction = pn->action;
	int	 redo;

	bzero(ruleaction, sizeof(ruleaction));
 redo:
	redo = 0;
	bzero(keyaction, sizeof(keyaction));
	bzero(keytype, sizeof(keytype));

	switch (pn->type) {
	case NODE_TYPE_HEADER:
		snprintf(keytype, sizeof(keytype), "header ");
		break;
	case NODE_TYPE_QUERY:
		snprintf(keytype, sizeof(keytype), "query ");
		break;
	case NODE_TYPE_COOKIE:
		snprintf(keytype, sizeof(keytype), "cookie ");
		break;
	case NODE_TYPE_PATH:
		snprintf(keytype, sizeof(keytype), "path ");
		break;
	case NODE_TYPE_URL:
		snprintf(keytype, sizeof(keytype), "url ");
		break;
	default:
		fatal("wrong rule");
	}

	if (pn->mark != 0) {
		if (pn->action == NODE_ACTION_MARK)
			(void)asprintf(&mark, " tag \"%d\"", pn->mark);
		else
			(void)asprintf(&mark, " tagged \"%d\"", pn->mark);
	}

	switch (nodeaction) {
	case NODE_ACTION_APPEND:
		snprintf(keyaction, sizeof(keyaction), "append ");
		break;
	case NODE_ACTION_CHANGE:
		snprintf(keyaction, sizeof(keyaction), "set ");
		break;
	case NODE_ACTION_REMOVE:
		snprintf(keyaction, sizeof(keyaction), "remove ");
		break;
	case NODE_ACTION_HASH:
		snprintf(keyaction, sizeof(keyaction), "hash ");
		break;
	case NODE_ACTION_LOG:
		snprintf(keyaction, sizeof(keyaction), "log ");
		break;
	case NODE_ACTION_EXPECT:
		(void)asprintf(&buf, "block %s%s",
		    pn->dir == RELAY_DIR_REQUEST ? "request " : "response ",
		    !keytype[0] ? "" : keytype);
		nodeaction = NODE_ACTION_NONE;
		snprintf(ruleaction, sizeof(ruleaction), "pass ");
		redo = 1;
		goto done;
		break;
	case NODE_ACTION_FILTER:
		snprintf(ruleaction, sizeof(ruleaction), "block ");
		quick = "quick ";
		break;
	case NODE_ACTION_MARK:
	case NODE_ACTION_NONE:
	default:
		break;
	}

	(void)asprintf(&buf, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
	    !ruleaction[0] ? "match " : ruleaction,
	    pn->dir == RELAY_DIR_REQUEST ? "request " : "response ",
	    quick == NULL ? "" : quick,
	    !keytype[0] ? "" : keytype,
	    !keyaction[0] ? "" : keyaction,
	    pn->flags & PNFLAG_DIGEST ? "digest " : "",
	    pn->key == NULL ? "" : "\"",
	    pn->key == NULL ? "" : pn->key,
	    pn->key == NULL ? "" : "\"",
	    pn->value == NULL ? "" : " value \"",
	    pn->value == NULL ? "" : pn->value,
	    pn->value == NULL ? "" : "\"",
	    mark == NULL ? "" : mark,
	    pn->flags & PNFLAG_NOLABEL ? " no label" : "",
	    pn->labelname == NULL ? "" : " label \"",
	    pn->labelname == NULL ? "" : pn->labelname,
	    pn->labelname == NULL ? "" : "\"");

 done:
	proto->rules = opts_add(proto->rules, &proto->rulesc, buf);

	if (mark != NULL) {
		free(mark);
		mark = NULL;
	}
	if (buf != NULL) {
		free(buf);
		buf = NULL;
	}
	if (redo)
		goto redo;
}

char *
getsslflag(u_int8_t f)
{
	switch (f) {
	case SSLFLAG_SSLV2:
		return ("sslv2");
		break;
	case SSLFLAG_TLSV1:
		return ("tlsv1");
		break;
	case SSLFLAG_SSLV3:
		return ("sslv3");
		break;
	case SSLFLAG_VERSION:
		return ("version");
		break;
	default:
		return ("");
		break;
	}
}

char *
getprototype(struct protocol *p)
{
	switch (p->type) {
	case RELAY_PROTO_HTTP:
		return ("http");
		break;
	case RELAY_PROTO_DNS:
		return ("dns");
		break;
	case RELAY_PROTO_TCP:
	default:
		/* tcp default is empty */
		return (NULL);
		break;
	}
}
