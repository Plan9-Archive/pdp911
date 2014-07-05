< /$objtype/mkfile

TARG=pdp11
OFILES=\
	main.$O\
	fs.$O\
	cpu.$O\
	mem.$O\
	kl.$O\
	rk.$O\
	clock.$O

HFILES=pdp11.h

BIN=$home/bin/$objtype

< /sys/src/cmd/mkone
