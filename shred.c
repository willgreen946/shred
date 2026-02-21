/*
 * Author - Bill Green
 * 
 * An implementation of the shred command for OpenBSD.
 * This could really be used on any UNIX like operating system but I designed
 * it for OpenBSD as it does not have a shred implementation.
 *
 * This will write X amount of null data equal to the length of the input file
 * and then write X amount of "random" data equal to the length of the input
 * file. The "random" data is gathered from /dev/urandom or /dev/random
 *
 * Essentially the same as doing "cat /dev/urandom > example.txt" in the shell
 * but it wont write more data than was originally there.
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>

#define OPT_DEBUG 0

enum {
  TYPE_FILE,
  TYPE_DIR,
  TYPE_SYM,
  TYPE_UNK
};

enum {
  DEF_SAFE       = 0,
  DEF_VERBOSE    = 0,
  DEF_RECURSIVE  = 0,
  DEF_ITERATIONS = 3
};

struct shred_opts {
  size_t blocks;   /* the amount of blocks of memory we will allocate */
  size_t block_len;/* the size of each block of memory we will allocate */
  int safe;        /* if true, write 1 byte at a time, will take a while */
  int verbose;     /* display exactly what the programs doing at run time */
  int recursive;   /* shredding whole directories */
  int interations; /* how many times to perform the shred on each file */
};

int shred(struct shred_opts *, const char *);
//int readopts(struct shred_opts *, int, const char *);
int fileshred(struct shred_opts *, const char *);
int dirshred(struct shred_opts *, const char *);
int doshred(struct shred_opts *, const char *, int, int, size_t);
void getblocks(struct shred_opts *, size_t);
int exists(const char *);
int getfiletype(const char *);
off_t getfildeslen(const char *, int);
void *readin(const char *, size_t);

int
main(int argc, const char **argv)
{
  struct shred_opts opts;

  /* set default options */
  opts.safe        = DEF_SAFE;
  opts.verbose     = DEF_VERBOSE;
  opts.recursive   = DEF_RECURSIVE;
  opts.interations = DEF_ITERATIONS;

  //if (readopts(&opts, argc, argv) < 0)
    //return -1;

  while (*++argv)
    shred(&opts, *argv);

  return 0;
}

//int
//readopts(struct shred_opts *, int, const char *);

int
shred(struct shred_opts *opts, const char *path)
{
  int ftype;

  if (!path || !opts)
    return -1;

  if (!exists(path))
    return -1;

  ftype = getfiletype(path);

  switch (ftype) {
  case TYPE_FILE:  
    return fileshred(opts, path);
  case TYPE_DIR:
    return dirshred(opts, path);
  default:
    fprintf(stderr,
      "%s : %s : Unsupported file type\n", __func__, path);
    return -1;
  }
}

int
fileshred(struct shred_opts *opts, const char *path)
{
  off_t len  = 0;
  int fildes = -1;

  /* attempt to open the path as a file */
  if ((fildes = open(path, O_RDWR)) < 0) {
    fprintf(stderr, "%s : %s : %s\n",
      __func__, path, strerror(errno));
    return -1;
  }

  /* get the file size */
  if ((len = getfildeslen(path, fildes)) < 0)
    return -1;

  /* actually start "shredding" the file */
  if (doshred(opts, path, fildes, 3, (size_t) len) < 0) {
    close(fildes);
    return -1;
  }

  close(fildes);

  return 0;
}

int
dirshred(struct shred_opts *opts, const char *path)
{
  if (!opts->recursive) {
    fprintf(stderr, "%s : %s : path given is a directory\n",
      __func__, path);
    return -1;
  }

  return 0;
}

int
getfiletype(const char *path)
{
  struct stat st;

  if (stat(path, &st) < 0) {
    fprintf(stderr, "%s : %s : %s\n",
      __func__, path, strerror(errno));
    return -1;
  }

  /* test file type */
  if (S_ISREG(st.st_mode) != 0) /* regular file */
    return TYPE_FILE;

  if (S_ISDIR(st.st_mode) != 0) /* directory */
    return TYPE_DIR;

  return TYPE_UNK;
}

int
doshred(struct shred_opts *opts, const char *path, int fildes, int loops,
        size_t len)
{
  void *randbuf;

  getblocks(opts, len);

  for (int i = 0; i < loops; i++) {
    /*
     * the actual shredding process,
     * get X amount of null data with a length equal to the length of the
     * file and write it to the file and then get X amount of random data
     * with a length equal to the length of the file and write it to the file
     */

    /* reset file position to avoid increasing file size with each loop */
    if (lseek(fildes, (off_t) 0, SEEK_SET) < 0)
      fprintf(stderr, "%s : %s\n", __func__, strerror(errno));

    for (size_t k = 0; k < opts->blocks; k++) {

      /* write null byte(s) */
      write(fildes, (void *) 0, opts->block_len);

      /* get & write random data */
      if (!(randbuf = readin("/dev/urandom", (size_t) opts->block_len)))
        goto fail;

      write(fildes, (void *) randbuf, opts->block_len);
      memset((void *) randbuf, 0, opts->block_len);
      free(randbuf);
    }
  }

  return 0;

fail:
  fprintf(stderr,
    "%s : %s : %s\n",
    __func__, path, strerror(errno));

  if (randbuf != (void *) 0)
    free(randbuf);

  return -1;
}

void
getblocks(struct shred_opts *opts, size_t len)
{
  struct rlimit rl;

  /* 1 byte at a time */
  opts->blocks    = len;
  opts->block_len = 1;


  if (!opts->safe) {
    if (getrlimit(RLIMIT_DATA, &rl) < 0) {
      opts->safe = 1;

      if (opts->verbose)
        fprintf(stderr,
          "%s : %s :"
          "enabling safe mode (this will take a long time for larger files)\n",
          __func__, strerror(errno));

      opts->blocks    = len;
      opts->block_len = 1;
    } else {
      /*
       * TODO Test my theory below:
       * number of blocks = amount of available memory / the length of the file
       * plus 1 to account for rounding errors (not sure if this is needed)
       *
       * the length of each block is the total length of the file / the amount
       * of blocks we have
       *
       * if the block length is <= 0 then the file is probably small enough to
       * be allocated as 1 block. PROBABLY, i would need an external opinion
       */
      opts->blocks    = (rl.rlim_cur / len) + 1;
      opts->block_len = len / opts->blocks;

      /* file is small enough to allocate all at once */
      if (opts->block_len <= 0) {
        opts->blocks    = 1;
        opts->block_len = len;
      }
    }
  }

#if defined(OPT_DEBUG)
  fprintf(stdout, "(debug) : function = %s, blocks = %zu, block length = %zu\n",
    __func__, opts->blocks, opts->block_len);
#endif
}

int
exists(const char *path)
{
  /* make sure specified path exists on the file system */
  if (access(path, F_OK) < 0) {
    fprintf(stderr,
      "%s : %s : No such file or directory\n",
      __func__, path);
    return 0;
  }

  return 1;
}

off_t
getfildeslen(const char *path, int fildes)
{
  off_t pos, len;

  pos = len = 0;

  /* get current position in the file */
  if ((pos = lseek(fildes, 0, SEEK_CUR)) < 0)
    goto fail;

  /* seek to end of file to get the length */
  if ((len = lseek(fildes, 0, SEEK_END)) < 0)
    goto fail;

  /* reset file position */
  if (lseek(fildes, pos, SEEK_SET) < 0)
    goto fail;

  return len;

fail:
  fprintf(stderr,
    "%s : %s : %s\n",
    __func__, path, strerror(errno));
  return (off_t) -1;
}

void *
readin(const char * path, size_t len)
{
  int fildes = -1;
  void *buf = (void *) 0;

  /* allocate memory for the buffer */
  if (!(buf = (void *) malloc((size_t) len))) {
    fprintf(stderr, "%s : Insufficient memory to allocate %ld bytes\n",
      __func__, len);
    return (void *) 0;
  }

  /* open the specified path */
  if ((fildes = open(path, O_RDONLY)) < 0)
    goto fail;

  /* read X amount of bytes into the buffer from the path */
  if (read(fildes, buf, len) < 0)
    goto fail;

  close(fildes);

  return buf;

fail:
  fprintf(stderr, "%s : %s : %s\n",
    __func__, path, strerror(errno));

  if (fildes >= 0)
    close(fildes);

  return buf;
}
