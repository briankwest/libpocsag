noinst_PROGRAMS = encode_page decode_stream modem_demo gen_wav

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

modem_demo_SOURCES = modem_demo.c
modem_demo_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
modem_demo_LDADD = $(top_builddir)/src/libpocsag.la -lm

gen_wav_SOURCES = gen_wav.c
gen_wav_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
gen_wav_LDADD = $(top_builddir)/src/libpocsag.la -lm
