check_PROGRAMS = test_libpocsag

test_libpocsag_SOURCES = \
	test_main.c \
	test_bch.c \
	test_codeword.c \
	test_numeric.c \
	test_alpha.c \
	test_encoder.c \
	test_decoder.c \
	test_roundtrip.c \
	test_modulator.c \
	test_demodulator.c

noinst_HEADERS = test.h

test_libpocsag_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include

test_libpocsag_LDADD = $(top_builddir)/src/libpocsag.la -lm

TESTS = test_libpocsag
