#include "pdp11.h"

int cons, ctl;

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
ctlwrite(Req *r)
{
	char *cmd;
	int n;

	cmd = r->ifcall.data;
	n = r->ofcall.count = r->ifcall.count;
	if(strncmp(cmd, "halt", strlen("halt")) == 0)
		running = 0;
}

void
cpuproc(void*)
{
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
	threadexitsall(nil);
}

void
usage(void)
{
	fprint(2, "usage: %s [-D] [-s srv] [-m mnt]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	int fd[2];
//	int srv;
	char *mnt, *srv;

	srv = "pdp11";
	mnt = "/mnt/pdp11";
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mnt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();

	dirtab[Qctl].write = ctlwrite;

	pipe(fd);
	dirtab[Qcon].fd = fd[0];
	cons = fd[1];

	proccreate(cpuproc, nil, mainstacksize);

	startfilesys(mnt, srv);

	threadexits(nil);

/*
	savesp();
	print("%06o %06o %06o %06o %06o %06o %06o [%06uo, %06uo] %06o %06o\n",
		r[0], r[1], r[2], r[3], r[4], r[5], r[6], ksp, usp, r[7], psw);
	printintrq(intrq);
	close(cons);
	print("exit done\n");
	threadexitsall(nil);
*/
}
