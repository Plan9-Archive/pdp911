#include "pdp11.h"

static Reqqueue *readqueue;

Dirtab dirtab[]=
{
	[Qdir]    { ".",      Qdir,    QTDIR,  0550|DMDIR, -1, nil, nil },
	[Qctl]    { "ctl",    Qctl,    QTFILE, 0660, -1, nil, nil },
	[Qcon]    { "con",    Qcon,    QTFILE, 0660, -1, nil, nil },
	[Qstatus] { "status", Qstatus, QTFILE, 0660, -1, nil, nil }
};

static int
dirgen(int n, Dir *d, void*)
{
	Dirtab *dt;

	d->atime = time(nil);
	d->mtime = d->atime;
	d->uid = estrdup9p(getuser());
	d->gid = estrdup9p(d->uid);
	d->muid = estrdup9p(d->uid);
	if(n == -1){
		dt = &dirtab[Qdir];
		d->length = 0;
	}else if(n >= 0 && n < QMAX-Qctl){
		dt = &dirtab[n+Qctl];
		d->length = dt->path;
	}else
		return -1;
	d->qid = (Qid){dt->path, 0, dt->type};
	d->mode = dt->mode;
	d->name = estrdup9p(dt->name);
	return 0;
}

static void
fsattach(Req *r)
{
	r->fid->qid = (Qid){Qdir, 0, QTDIR};
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Qid q;
	int i;
	Dirtab *dt;

	q = fid->qid;
	if(q.type & QTDIR)
		for(i = Qctl; i < QMAX; i++){
			dt = &dirtab[i];
			if(strcmp(name, dt->name) == 0){
				*qid = (Qid){dt->path, 0, dt->type};
				fid->qid = *qid;
				return nil;
			}
		}
	else
		if(strcmp(name, "..") == 0){
			*qid = (Qid){Qdir, 0, QTDIR};
			fid->qid = *qid;
			return nil;
		}
	return "no such file";
}

static void
fsstat(Req *r)
{
	Qid q;
	q = r->fid->qid;
	if(q.type & QTDIR)
		dirgen(-1, &r->d, nil);
	else
		dirgen(q.path-Qctl, &r->d, nil);
	respond(r, nil);
}

static void
fsflush(Req *r)
{
	reqqueueflush(readqueue, r->oldreq);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	Qid q;
	Dirtab *dt;
	int n;

	q = r->fid->qid;

	if(q.type & QTDIR){
		respond(r, "permission denied");
		return;
	}

	dt = &dirtab[q.path];
	if(dt->fd >= 0){
		n = write(dt->fd, r->ifcall.data, r->ifcall.count);
		r->ofcall.count = n;
	}else if(dt->write){
		dt->write(r);
	}else{
		print("write to %s\n", dt->name);
		n = write(1, r->ifcall.data, r->ifcall.count);
		r->ofcall.count = n;
	}
	respond(r, nil);
}

static void
readproc(Req *r)
{
	int n;
	Dirtab *dt;
	dt = &dirtab[r->fid->qid.path];
	n = read(dt->fd, r->ofcall.data, r->ifcall.count);
	if(n < 0){
		respond(r, "interrupted");
	}else{
		r->ofcall.count = n;
		respond(r, nil);
	}
}

static void
fsread(Req *r)
{
	Qid q;
	Dirtab *dt;
	char s[20];

	q = r->fid->qid;
	if(q.type & QTDIR)
		dirread9p(r, dirgen, nil);
	else{
		dt = &dirtab[q.path];
		if(dt->fd >= 0){
			reqqueuepush(readqueue, r, readproc);
			return;
		}else if(dt->read){
			dt->read(r);
		}else{
			sprint(s, "read from %s\n", dt->name);
			readstr(r, s);
		}
	}
	respond(r, nil);
}

static Srv fssrv = {
	.attach		fsattach,
	.walk1		fswalk1,
	.stat		fsstat,
	.write		fswrite,
	.read		fsread,
	.flush		fsflush
};

void
startfilesys(char *mnt, char *srv)
{
	readqueue = reqqueuecreate();
	threadpostmountsrv(&fssrv, srv, mnt, MREPL|MCREATE);
}
