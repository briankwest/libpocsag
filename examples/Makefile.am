noinst_PROGRAMS = encode_page decode_stream

encode_page_SOURCES = encode_page.c
encode_page_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
encode_page_LDADD = $(top_builddir)/src/libpocsag.la

decode_stream_SOURCES = decode_stream.c
decode_stream_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
decode_stream_LDADD = $(top_builddir)/src/libpocsag.la
