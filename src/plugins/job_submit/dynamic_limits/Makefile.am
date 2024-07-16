# Makefile for job_submit/dynamic_limits plugin
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

# old: AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_dynamic_limits.la

job_submit_dynamic_limits_la_SOURCES = job_submit_dynamic_limits.c dynamic_limits_config.c
job_submit_dynamic_limits_la_LDFLAGS = $(PLUGIN_FLAGS)