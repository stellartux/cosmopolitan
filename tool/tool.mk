#-*-mode:makefile-gmake;indent-tabs-mode:t;tab-width:8;coding:utf-8-*-┐
#───vi: set et ft=make ts=8 tw=8 fenc=utf-8 :vi───────────────────────┘

.PHONY:	o/$(MODE)/tool
o/$(MODE)/tool:			\
	o/$(MODE)/tool/args	\
	o/$(MODE)/tool/build	\
	o/$(MODE)/tool/curl	\
	o/$(MODE)/tool/decode	\
	o/$(MODE)/tool/hello	\
	o/$(MODE)/tool/lambda	\
	o/$(MODE)/tool/net	\
	o/$(MODE)/tool/plinko	\
	o/$(MODE)/tool/viz
