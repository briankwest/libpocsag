noinst_PROGRAMS = encode_page decode_stream modem_demo gen_wav gen_baseband

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

gen_baseband_SOURCES = gen_baseband.c
gen_baseband_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
gen_baseband_LDADD = $(top_builddir)/src/libpocsag.la

noinst_PROGRAMS += pocsag_tx
pocsag_tx_SOURCES = pocsag_tx.c
pocsag_tx_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include
pocsag_tx_LDADD = $(top_builddir)/src/libpocsag.la -lportaudio -lm

if HAVE_RTLSDR
noinst_PROGRAMS += pocsag_sdr
pocsag_sdr_SOURCES = pocsag_sdr.c
pocsag_sdr_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(RTLSDR_CFLAGS)
pocsag_sdr_LDADD = $(top_builddir)/src/libpocsag.la $(RTLSDR_LIBS) -lm -lpthread
endif
