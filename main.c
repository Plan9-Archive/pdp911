#include "pdp11.h"

int cons;

void
loadmem(char *fname)
{
	Biobuf *in;
	char *s;
	int p;

	in = Bopen(fname, OREAD);
	if(in == nil){
		fprint(2, "file open error: %r\n");
		exits("file");
	}
	p = 0;
	while(s = Brdstr(in, '\n', 1)){
		while(isspace(*s))
			s++;
		if(*s == ':')
			p = strtol(s+1, nil, 8) >> 1;
		if(*s >= '0' && *s <= '7')
			coremem[p++] = strtol(s, nil, 8);
	}
	Bterm(in);
}

void
usage(void)
{
	fprint(2, "usage: %s memfile\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	int fd[2];
	int srv;

	USED(argc);
	USED(argv);
/*
	ARGBEGIN{
	default:
		usage();
	}ARGEND;
	if(argc < 1)
		usage();
*/

	pipe(fd);
	srv = create("/srv/pdp11", OWRITE | ORCLOSE, 0644);
	if(srv < 0)
		sysfatal("create: %r");
	fprint(srv, "%d", fd[0]);
	close(fd[0]);
	cons = fd[1];
	fd[0] = fd[1] = -1;

//	loadmem(argv[0]);
	initbus();
	initcpu();
//	sr = 0173030;
	sr = 1;
	clockinit();
	klinit();
	rkinit();
	rkattach(0, "disk0.rk");
	rkattach(1, "disk1.rk");
	rkattach(2, "disk2.rk");
	rkboot(0);
	savesp();
	print("%06o %06o %06o %06o %06o %06o %06o [%06uo, %06uo] %06o %06o\n",
		r[0], r[1], r[2], r[3], r[4], r[5], r[6], ksp, usp, r[7], psw);
	printintrq(intrq);
	close(cons);
	threadexitsall(nil);
}
