#ifndef _EXTRAMETA_H
#define _EXTRAMETA_H

#define META_ACCESS_ACL		'A'
#define META_DEFAULT_ACL	'D'
#define META_XATTR		'X'
#define META_XATTR_BSD		'B'
#define META_VSS		'V'

extern int has_extrameta(const char *path, enum cmd cmd, struct conf **confs);

extern int get_extrameta(struct asfd *asfd,
	BFILE *bfd,
	struct sbuf *sb,
	char **extrameta,
	size_t *elen,
	struct conf **confs);

extern int set_extrameta(struct asfd *asfd,
	BFILE *bfd,
	const char *path,
	struct sbuf *sb,
	const char *extrameta,
	size_t metalen,
	struct conf **confs);

#endif
