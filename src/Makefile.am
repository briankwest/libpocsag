lib_LTLIBRARIES = libpocsag.la

libpocsag_la_SOURCES = \
	error.c \
	bch.c \
	codeword.c \
	numeric.c \
	alpha.c \
	encoder.c \
	decoder.c

noinst_HEADERS = pocsag_internal.h

libpocsag_la_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include

libpocsag_la_LDFLAGS = -version-info 0:0:0
