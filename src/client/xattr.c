#include "include.h"

#ifdef HAVE_XATTR
#if defined(HAVE_LINUX_OS) \
 || defined(HAVE_FREEBSD_OS) \
 || defined(HAVE_NETBSD_OS) \
 || defined(HAVE_OPENBSD_OS)

static char *get_next_str(struct asfd *asfd, char **data, size_t *l,
	struct conf **confs, ssize_t *s, const char *path)
{
	char *ret=NULL;

	if((sscanf(*data, "%08X", (unsigned int *)s))!=1)
	{
		logw(asfd, confs, "sscanf of xattr '%s' %d failed for %s\n",
			*data, *l, path);
		return NULL;
	}
	*data+=8;
	*l-=8;
	if(!(ret=(char *)malloc_w((*s)+1, __func__)))
		return NULL;
	memcpy(ret, *data, *s);
	ret[*s]='\0';

	*data+=*s;
	*l-=*s;

	return ret;
}
#endif
#endif

#ifdef HAVE_XATTR
#if defined(HAVE_LINUX_OS)
#include <sys/xattr.h>

static const char *xattr_acl_skiplist[3] = { "system.posix_acl_access", "system.posix_acl_default", NULL };
//static const char *xattr_skiplist[1] = { NULL };

int has_xattr(const char *path, enum cmd cmd)
{
	if(llistxattr(path, NULL, 0)>0) return 1;
	return 0;
}

int get_xattr(struct asfd *asfd, struct sbuf *sb,
	char **xattrtext, size_t *xlen, struct conf **confs)
{
	char *z=NULL;
	size_t len=0;
	int have_acl=0;
	char *toappend=NULL;
	char *xattrlist=NULL;
	size_t totallen=0;
	size_t maxlen=0xFFFFFFFF/2;
	const char *path=sb->path.buf;

	if((len=llistxattr(path, NULL, 0))<=0)
	{
		logw(asfd, confs, "could not llistxattr '%s': %d\n", path, len);
		return 0; // carry on
	}
	if(!(xattrlist=(char *)calloc_w(1, len+1, __func__)))
		return -1;
	if((len=llistxattr(path, xattrlist, len))<=0)
	{
		logw(asfd, confs, "could not llistxattr '%s': %d\n", path, len);
		free_w(&xattrlist);
		return 0; // carry on
	}
	xattrlist[len]='\0';

	if(xattrtext && *xattrtext)
	{
		// Already have some meta text, which means that some
		// ACLs were set.
		have_acl++;
	}

	z=xattrlist;
	for(z=xattrlist; len > (size_t)(z-xattrlist)+1; z=strchr(z, '\0')+1)
	{
		char tmp1[9];
		char tmp2[9];
		char *val=NULL;
		size_t vlen=0;
		size_t zlen=0;

		if((zlen=strlen(z))>maxlen)
		{
                	logw(asfd, confs, "xattr element of '%s' too long: %d\n",
				path, zlen);
			free_w(&toappend);
			break;
		}

		if(have_acl)
		{
			int c=0;
			int skip=0;
			// skip xattr entries that were already saved as ACLs.
			for(c=0; xattr_acl_skiplist[c]; c++)
			{
				if(!strcmp(z, xattr_acl_skiplist[c]))
				{
					skip++;
					break;
				}
			}
			if(skip) continue;
		}

		if((vlen=lgetxattr(path, z, NULL, 0))<=0)
		{
			logw(asfd, confs,
				"could not lgetxattr on %s for %s: %d\n",
				path, z, vlen);
			continue;
		}
		if(vlen)
		{
			if(!(val=(char *)malloc_w(vlen+1, __func__)))
			{
				free_w(&xattrlist);
				free_w(&toappend);
				return -1;
			}
			if((vlen=lgetxattr(path, z, val, vlen))<=0)
			{
				logw(asfd, confs,
					"could not lgetxattr %s for %s: %d\n",
					path, z, vlen);
				free_w(&val);
				continue;
			}
			val[vlen]='\0';

			if(vlen>maxlen)
			{
				logw(asfd, confs,
					"xattr value of '%s' too long: %d\n",
					path, vlen);
				free_w(&toappend);
				free_w(&val);
				break;
			}
		}

		snprintf(tmp1, sizeof(tmp1), "%08X", (unsigned int)zlen);
		snprintf(tmp2, sizeof(tmp2), "%08X", (unsigned int)vlen);
		if(!(toappend=prepend_len(toappend, totallen,
			tmp1, 8, "", 0, &totallen))
		  || !(toappend=prepend_len(toappend, totallen,
			z, zlen, "", 0, &totallen))
		  || !(toappend=prepend_len(toappend, totallen,
			tmp2, 8, "", 0, &totallen))
		  || (vlen && !(toappend=prepend_len(toappend, totallen,
			val, vlen, "", 0, &totallen))))
		{
			log_out_of_memory(__func__);
			free_w(&val);
			free_w(&xattrlist);
			return -1;
		}
		free_w(&val);

		if(totallen>maxlen)
		{
                	logw(asfd, confs,
				"xattr length of '%s' grew too long: %d\n",
				path, totallen);
			free_w(&val);
			free_w(&toappend);
			free_w(&xattrlist);
			return 0; // carry on
		}
	}

	if(toappend)
	{
		char tmp3[10];
		snprintf(tmp3, sizeof(tmp3), "%c%08X",
			META_XATTR, (unsigned int)totallen);
		if(!(*xattrtext=prepend_len(*xattrtext, *xlen,
			tmp3, 9, "", 0, xlen))
		  || !(*xattrtext=prepend_len(*xattrtext, *xlen,
			toappend, totallen, "", 0, xlen)))
		{
			log_out_of_memory(__func__);
			free_w(&toappend);
			free_w(&xattrlist);
			return -1;
		}
		free_w(&toappend);
	}
	free_w(&xattrlist);
	return 0;
}

static int do_set_xattr(struct asfd *asfd,
	const char *path, struct sbuf *sb,
	const char *xattrtext, size_t xlen, struct conf **confs)
{
	size_t l=0;
	int ret=-1;
	char *data=NULL;
	char *name=NULL;
	char *value=NULL;

	data=(char *)xattrtext;
	l=xlen;
	while(l>0)
	{
		ssize_t s=0;
		free_w(&name);
		free_w(&value);

		if(!(name=get_next_str(asfd, &data, &l, confs, &s, path))
		  || !(value=get_next_str(asfd, &data, &l, confs, &s, path)))
			goto end;

		if(lsetxattr(path, name, value, strlen(value), 0))
		{
			logw(asfd, confs, "lsetxattr error on %s: %s\n",
				path, strerror(errno));
			goto end;
		}
	}

	ret=0;
end:
	free_w(&name);
	free_w(&value);
	return ret;
}

int set_xattr(struct asfd *asfd, const char *path, struct sbuf *sb,
	const char *xattrtext, size_t xlen, char metacmd, struct conf **confs)
{
	switch(metacmd)
	{
		case META_XATTR:
			return do_set_xattr(asfd,
				path, sb, xattrtext, xlen, confs);
		default:
			logp("unknown xattr type: %c\n", metacmd);
			logw(asfd, confs, "unknown xattr type: %c\n", metacmd);
			break;
	}
	return -1;
}

#endif // HAVE_LINUX_OS

#if defined(HAVE_FREEBSD_OS) \
 || defined(HAVE_NETBSD_OS) \
 || defined(HAVE_OPENBSD_OS)

#include <sys/extattr.h>
#include <libutil.h>

static int namespaces[2] = { EXTATTR_NAMESPACE_USER, EXTATTR_NAMESPACE_SYSTEM };

#if defined(HAVE_FREEBSD_OS)
static const char *acl_skiplist[2] = { "system.posix1e.acl_access", NULL };
#endif

int has_xattr(const char *path, enum cmd cmd)
{
	int i=0;
	for(i=0; i<(int)(sizeof(namespaces)/sizeof(int)); i++)
	{
		if(extattr_list_link(path, namespaces[i], NULL, 0)>0)
			return 1;
	}
	return 0;
}

#define BSD_BUF_SIZE	1024
int get_xattr(struct asfd *asfd, struct sbuf *sb,
	char **xattrtext, size_t *xlen, struct conf **confs)
{
	int i=0;
	size_t maxlen=0xFFFFFFFF/2;
	const char *path=sb->path.buf;

	for(i=0; i<(int)(sizeof(namespaces)/sizeof(int)); i++)
	{
		int j=0;
		size_t len=0;
		int have_acl=0;
		char *xattrlist=NULL;
		char *cnamespace=NULL;
		size_t totallen=0;
		char *toappend=NULL;
		char ctuple[BSD_BUF_SIZE]="";
		char cattrname[BSD_BUF_SIZE]="";
		if((len=extattr_list_link(path, namespaces[i], NULL, 0))<0)
		{
			logw(asfd, confs, "could not extattr_list_link of '%s': %d\n",
				path, len);
			return 0; // carry on
		}
		if(!len) continue;
		if(xattrtext && *xattrtext)
		{
			// Already have some meta text, which means that some
			// ACLs were set.
			have_acl++;
		}
		if(!(xattrlist=(char *)calloc_w(1, len+1, __func__)))
			return -1;
		if((len=extattr_list_link(path, namespaces[i], xattrlist, len))<=0)
		{
			logw(asfd, confs, "could not extattr_list_link '%s': %d\n",
				path, len);
			free_w(&xattrlist);
			return 0; // carry on
		}
		xattrlist[len]='\0';

		// Convert namespace number to string. It has to be freed
		// later on.
		if(extattr_namespace_to_string(namespaces[i], &cnamespace))
		{
			logp("Failed to convert %d into namespace on '%s'\n",
				 namespaces[i], path);
			free_w(&xattrlist);
			return 0; // carry on
		}


		for(j=0; j<(int)len; j+=xattrlist[j]+1)
		{
			int cnt=0;
			char tmp1[9];
			char tmp2[9];
			size_t zlen=0;
			size_t vlen=0;
			char *val=NULL;
			cnt=xattrlist[j];
			if(cnt>((int)sizeof(cattrname)-1))
				cnt=((int)sizeof(cattrname)-1);
			strncpy(cattrname, xattrlist+(j+1), cnt);
			cattrname[cnt]='\0';
			snprintf(ctuple, sizeof(ctuple), "%s.%s",
				cnamespace, cattrname);

			if(have_acl)
			{
				int c=0;
				int skip=0;
				// skip xattr entries that were already saved
				// as ACLs.
				for(c=0; acl_skiplist[c]; c++)
				{
					if(!strcmp(ctuple, acl_skiplist[c]))
					{
						skip++;
						break;
					}
				}
				if(skip) continue;
			}
			zlen=strlen(ctuple);
			//printf("\ngot: %s (%s)\n", ctuple, path);

			if((vlen=extattr_list_link(path, namespaces[i],
				xattrlist, len))<0)
			{
				logw(asfd, confs, "could not extattr_list_link on %s for %s: %d\n", path, namespaces[i], vlen);
				continue;
			}
			if(vlen)
			{
				if(!(val=(char *)malloc_w(vlen+1, __func__)))
				{
					free_w(&xattrlist);
					free_w(&toappend);
					return -1;
				}
				if((vlen=extattr_get_link(path, namespaces[i],
					cattrname, val, vlen))<0)
				{
					logw(asfd, confs, "could not extattr_list_link %s for %s: %d\n", path, namespaces[i], vlen);
					free_w(&val);
					continue;
				}
				val[vlen]='\0';

				if(vlen>maxlen)
				{
					logw(asfd, confs, "xattr value of '%s' too long: %d\n",
						path, vlen);
					free_w(&toappend);
					free_w(&val);
					break;
				}
			}

			snprintf(tmp1, sizeof(tmp1), "%08X", (unsigned)zlen);
			snprintf(tmp2, sizeof(tmp2), "%08X", (unsigned)vlen);
			if(!(toappend=prepend_len(toappend, totallen,
				tmp1, 8, "", 0, &totallen))
			  || !(toappend=prepend_len(toappend, totallen,
				ctuple, zlen, "", 0, &totallen))
			  || !(toappend=prepend_len(toappend, totallen,
				tmp2, 8, "", 0, &totallen))
			  || (vlen && !(toappend=prepend_len(toappend, totallen,
				val, vlen, "", 0, &totallen))))
			{
				log_out_of_memory(__func__);
				free_w(&val);
				free_w(&xattrlist);
				return -1;
			}
			free_w(&val);

			if(totallen>maxlen)
			{
				logw(asfd, confs, "xattr length of '%s' grew too long: %d\n",
					path, totallen);
				free_w(&val);
				free_w(&toappend);
				free_w(&xattrlist);
				return 0; // carry on
			}

			//printf("now: %s\n", toappend);
		}

		free_w(&cnamespace);

		if(toappend)
		{
			char tmp3[10];
			snprintf(tmp3, sizeof(tmp3), "%c%08X",
				META_XATTR_BSD, (unsigned)totallen);
			if(!(*xattrtext=prepend_len(*xattrtext, *xlen,
				tmp3, 9, "", 0, xlen))
			  || !(*xattrtext=prepend_len(*xattrtext, *xlen,
				toappend, totallen, "", 0, xlen)))
			{
				log_out_of_memory(__func__);
				free_w(&toappend);
				free_w(&xattrlist);
				return -1;
			}
			free_w(&toappend);
			//printf("and: %s %li\n", *xattrtext, *xlen);
		}
		free_w(&xattrlist);
	}

	return 0;
}

static int do_set_xattr_bsd(struct asfd *asfd,
	const char *path, struct sbuf *sb,
	const char *xattrtext, size_t xlen, struct conf **confs)
{
	int ret=-1;
	size_t l=0;
	char *data=NULL;
	char *value=NULL;
	char *nspace=NULL;

	data=(char *)xattrtext;
	l=xlen;
	while(l>0)
	{
		int cnt;
		ssize_t vlen=0;
		int cnspace=0;
		char *name=NULL;

		if(!(nspace=get_next_str(asfd, &data, &l, confs, &vlen, path))
		  || !(value=get_next_str(asfd, &data, &l, confs, &vlen, path)))
			goto end;

		// Need to split the name into two parts.
		if(!(name=strchr(nspace, '.')))
		{
			logw(asfd, confs,
			  "could not split %s into namespace and name on %s\n",
				nspace, path);
			goto end;
		}
		*name='\0';
		name++;

		if(extattr_string_to_namespace(nspace, &cnspace))
		{
			logw(asfd, confs,
				"could not convert %s into namespace on %s",
				nspace, path);
			goto end;
		}

		//printf("set_link: %d %s %s %s\n", cnspace, nspace, name, value);
		if((cnt=extattr_set_link(path,
			cnspace, name, value, vlen))!=vlen)
		{
			logw(asfd, confs,
				"extattr_set_link error on %s %d!=vlen: %s\n",
				path, strerror(errno));
			goto end;
		}

		free_w(&nspace);
		free_w(&value);
	}
	ret=0;
end:
	free_w(&nspace);
	free_w(&value);
	return ret;
}

int set_xattr(struct asfd *asfd, const char *path,
	struct sbuf *sb, const char *xattrtext,
	size_t xlen, char metacmd, struct conf **confs)
{
	switch(metacmd)
	{
		case META_XATTR_BSD:
			return do_set_xattr_bsd(asfd, path, sb,
				xattrtext, xlen, confs);
		default:
			logp("unknown xattr type: %c\n", metacmd);
			logw(asfd, confs, "unknown xattr type: %c\n", metacmd);
			break;
	}
	return -1;
}

#endif // HAVE_FREE/NET/OPENBSD_OS

#endif // HAVE_XATTR
