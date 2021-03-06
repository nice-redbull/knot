ACLOCAL_AMFLAGS = -I $(top_srcdir)/m4
SUBDIRS = . tests

sbin_PROGRAMS = knotc knotd kdig khost knsupdate
noinst_PROGRAMS = zscanner-tool
noinst_LTLIBRARIES = libknot.la libknotd.la libknots.la libzscanner.la

# $(YACC) will generate header file
AM_CPPFLAGS = -I$(top_srcdir)/src/libknot -DSYSCONFDIR='"$(sysconfdir)"' -DSBINDIR='"$(sbindir)"'
AM_YFLAGS = -d
libknotd_la_YFLAGS = -pcf_ -d
libknotd_la_LFLAGS = # TODO: reentrant parser, prefix

EXTRA_DIST =					\
	zscanner/scanner.rl			\
	zscanner/scanner_body.rl		\
	zscanner/test/run_tests.sh		\
	zscanner/test/cases

BUILT_SOURCES =					\
	knot/conf/libknotd_la-cf-lex.c		\
	knot/conf/libknotd_la-cf-parse.c	\
	knot/conf/libknotd_la-cf-parse.h

CLEANFILES =					\
	knot/conf/libknotd_la-cf-lex.c		\
	knot/conf/libknotd_la-cf-parse.c	\
	knot/conf/libknotd_la-cf-parse.h

if HAVE_RAGEL
BUILT_SOURCES += zscanner/scanner.c
CLEANFILES += zscanner/scanner.c

zscanner/scanner.c: zscanner/scanner.rl zscanner/scanner_body.rl
	$(RAGEL) $(FSM_TYPE) -s -o $@ $(srcdir)/zscanner/scanner.rl

endif

knotc_SOURCES =					\
	knot/ctl/knotc_main.c

knotd_SOURCES =					\
	knot/main.c

kdig_SOURCES =					\
	utils/common/msg.h			\
	utils/common/msg.c			\
	utils/common/exec.c			\
	utils/common/exec.h			\
	utils/common/params.c			\
	utils/common/params.h			\
	utils/common/resolv.c			\
	utils/common/resolv.h			\
	utils/common/netio.c			\
	utils/common/netio.h			\
	utils/common/token.c			\
	utils/common/token.h			\
	utils/dig/dig_main.c			\
	utils/dig/dig_params.c			\
	utils/dig/dig_params.h			\
	utils/dig/dig_exec.c			\
	utils/dig/dig_exec.h

khost_SOURCES =					\
	utils/common/msg.h			\
	utils/common/msg.c			\
	utils/common/exec.c			\
	utils/common/exec.h			\
	utils/common/params.c			\
	utils/common/params.h			\
	utils/common/resolv.c			\
	utils/common/resolv.h			\
	utils/common/netio.c			\
	utils/common/netio.h			\
	utils/common/token.c			\
	utils/common/token.h			\
	utils/host/host_main.c			\
	utils/dig/dig_params.c			\
	utils/dig/dig_params.h			\
	utils/host/host_params.c		\
	utils/host/host_params.h		\
	utils/dig/dig_exec.c			\
	utils/dig/dig_exec.h

knsupdate_SOURCES =				\
	utils/common/msg.h			\
	utils/common/msg.c			\
	utils/common/params.c			\
	utils/common/params.h			\
	utils/common/resolv.c			\
	utils/common/resolv.h			\
	utils/common/netio.c			\
	utils/common/netio.h			\
	utils/common/token.c			\
	utils/common/token.h			\
	utils/common/exec.c			\
	utils/common/exec.h			\
	utils/nsupdate/nsupdate_main.c		\
	utils/nsupdate/nsupdate_params.h	\
	utils/nsupdate/nsupdate_params.c	\
	utils/nsupdate/nsupdate_exec.h		\
	utils/nsupdate/nsupdate_exec.c

libknot_la_SOURCES =				\
	libknot/common.h			\
	libknot/consts.h			\
	libknot/consts.c			\
	libknot/util/utils.c			\
	libknot/util/debug.c			\
	libknot/util/debug.h			\
	libknot/util/utils.h			\
	libknot/util/tolower.h			\
	libknot/util/tolower.c			\
	libknot/util/wire.h			\
	libknot/util/endian.h			\
	libknot/packet/query.c			\
	libknot/packet/response.c		\
	libknot/packet/packet.c			\
	libknot/packet/packet.h			\
	libknot/packet/query.h			\
	libknot/packet/response.h		\
	libknot/zone/zone.c			\
	libknot/zone/zone-contents.c		\
	libknot/zone/zone-tree.c		\
	libknot/zone/zone-tree.h		\
	libknot/zone/node.h			\
	libknot/zone/zone.h			\
	libknot/zone/zone-contents.h		\
	libknot/zone/zonedb.c			\
	libknot/zone/zonedb.h			\
	libknot/zone/node.c			\
	libknot/zone/zone-diff.h		\
	libknot/zone/zone-diff.c		\
	libknot/nameserver/name-server.h	\
	libknot/nameserver/name-server.c	\
	libknot/nameserver/chaos.h		\
	libknot/nameserver/chaos.c		\
	libknot/updates/changesets.h		\
	libknot/updates/changesets.c		\
	libknot/updates/xfr-in.h		\
	libknot/updates/xfr-in.c		\
	libknot/updates/ddns.h			\
	libknot/updates/ddns.c			\
	libknot/dname.c				\
	libknot/nsec3.c				\
	libknot/consts.h			\
	libknot/edns.h				\
	libknot/edns.c				\
	libknot/libknot.h			\
	libknot/dname.h				\
	libknot/rrset.h				\
	libknot/rrset.c				\
	libknot/nsec3.h				\
	libknot/rrset-dump.h			\
	libknot/rrset-dump.c			\
	libknot/tsig.h				\
	libknot/tsig.c				\
	libknot/tsig-op.h			\
	libknot/tsig-op.c			\
	libknot/binary.h			\
	libknot/binary.c			\
	libknot/sign/key.h			\
	libknot/sign/key.c			\
	libknot/sign/bnutils.h			\
	libknot/sign/bnutils.c			\
	libknot/sign/dnssec.h			\
	libknot/sign/dnssec.c			\
	libknot/sign/sig0.h			\
	libknot/sign/sig0.c

libknots_la_SOURCES =				\
	common/slab/slab.c			\
	common/slab/slab.h			\
	common/slab/alloc-common.h		\
	common/libtap/tap.c			\
	common/libtap/tap.h			\
	common/libtap/tap_unit.h		\
	common/atomic.h				\
	common/mempattern.h			\
	common/mempattern.c			\
	common/descriptor.h			\
	common/descriptor.c			\
	common/lists.c				\
	common/base64.c				\
	common/base64.h				\
	common/lists.h				\
	common/heap.h				\
	common/heap.c				\
	common/print.c				\
	common/print.h				\
	common/skip-list.c			\
	common/base32hex.c			\
	common/skip-list.h			\
	common/base32hex.h			\
	common/evqueue.h			\
	common/evqueue.c			\
	common/queue.h				\
	common/queue.c				\
	common/evsched.h			\
	common/evsched.c			\
	common/acl.h				\
	common/acl.c				\
	common/sockaddr.h			\
	common/sockaddr.c			\
	common/crc.h				\
	common/ref.h				\
	common/ref.c				\
	common/errors.h				\
	common/errors.c				\
	common/errcode.h			\
	common/errcode.c			\
	common/dSFMT.h				\
	common/dSFMT-params.h			\
	common/dSFMT-params521.h		\
	common/dSFMT.c				\
	common/prng.h				\
	common/prng.c				\
	common/fdset.h				\
	common/fdset.c				\
	common/fdset_poll.h			\
	common/fdset_poll.c			\
	common/fdset_kqueue.h			\
	common/fdset_kqueue.c			\
	common/fdset_epoll.h			\
	common/fdset_epoll.c			\
	common/getline.h			\
	common/getline.c			\
	common/log.c				\
	common/log.h				\
	common/mempool.c			\
	common/mempool.h			\
	common/hattrie/ahtable.c		\
	common/hattrie/ahtable.h		\
	common/hattrie/hat-trie.c		\
	common/hattrie/hat-trie.h		\
	common/hattrie/murmurhash3.c		\
	common/hattrie/murmurhash3.h

libknotd_la_SOURCES =				\
	knot/stat/gatherer.c			\
	knot/stat/stat.c			\
	knot/stat/gatherer.h			\
	knot/stat/stat.h			\
	knot/stat/stat-common.h			\
	knot/common.h				\
	common/latency.h			\
	knot/other/debug.h			\
	knot/conf/cf-lex.l			\
	knot/conf/cf-parse.y			\
	knot/conf/conf.c			\
	knot/conf/conf.h			\
	knot/conf/extra.c			\
	knot/conf/extra.h			\
	knot/conf/includes.c			\
	knot/conf/includes.h			\
	knot/conf/logconf.c			\
	knot/conf/logconf.h			\
	knot/ctl/process.c			\
	knot/ctl/process.h			\
	knot/ctl/remote.c			\
	knot/ctl/remote.h			\
	knot/server/dthreads.c			\
	knot/server/journal.c			\
	knot/server/socket.c			\
	knot/server/server.c			\
	knot/server/udp-handler.c		\
	knot/server/tcp-handler.c		\
	knot/server/xfr-handler.c		\
	knot/server/zones.c			\
	knot/server/socket.h			\
	knot/server/udp-handler.h		\
	knot/server/tcp-handler.h		\
	knot/server/xfr-handler.h		\
	knot/server/dthreads.h			\
	knot/server/journal.h			\
	knot/server/zones.h			\
	knot/server/notify.h			\
	knot/server/notify.c			\
	knot/server/zones.h			\
	knot/server/rrl.h			\
	knot/server/rrl.c			\
	knot/zone/semantic-check.c		\
	knot/zone/semantic-check.h		\
	knot/zone/zone-dump.h			\
	knot/zone/zone-dump.c			\
	knot/zone/zone-load.h			\
	knot/zone/zone-load.c			\
	knot/server/server.h

zscanner_tool_SOURCES =				\
	zscanner/test/zscanner-tool.c		\
	zscanner/test/tests.h			\
	zscanner/test/tests.c			\
	zscanner/test/processing.h		\
	zscanner/test/processing.c

libzscanner_la_SOURCES =			\
	zscanner/file_loader.h			\
	zscanner/file_loader.c			\
	zscanner/scanner.h			\
	zscanner/scanner.c			\
	zscanner/scanner_functions.h		\
	zscanner/scanner_functions.c

libknotd_la_LIBADD = libknot.la libknots.la @LIBOBJS@
libknots_la_LIBADD = libzscanner.la @LIBOBJS@
libzscanner_la_LIBADD = @LIBOBJS@
knotd_LDADD = libknotd.la libknot.la libknots.la @LIBOBJS@
knotc_LDADD = libknotd.la libknot.la libknots.la @LIBOBJS@
kdig_LDADD = libknotd.la libknot.la libknots.la @LIBOBJS@
khost_LDADD = libknotd.la libknot.la libknots.la @LIBOBJS@
knsupdate_LDADD = libknotd.la libknot.la libknots.la libzscanner.la @LIBOBJS@
zscanner_tool_LDADD = libknots.la libknot.la libknotd.la libzscanner.la @LIBOBJS@
