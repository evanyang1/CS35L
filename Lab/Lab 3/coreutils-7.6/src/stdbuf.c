/* stdbuf -- setup the standard streams for a command
   Copyright (C) 2009 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Pádraig Brady.  */

#include <config.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <assert.h>

#include "system.h"
#include "error.h"
#include "posixver.h"
#include "quote.h"
#include "xstrtol.h"
#include "c-ctype.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "stdbuf"
#define LIB_NAME "libstdbuf.so" /* FIXME: don't hardcode  */

#define AUTHORS proper_name_utf8 ("Padraig Brady", "P\303\241draig Brady")

/* Internal error  */
enum { EXIT_CANCELED = 125 };

static char *program_path;

extern char **environ;

static struct
{
  size_t size;
  int optc;
  char *optarg;
} stdbuf[3];

static struct option const longopts[] =
{
  {"input", required_argument, NULL, 'i'},
  {"output", required_argument, NULL, 'o'},
  {"error", required_argument, NULL, 'e'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

/* Set size to the value of STR, interpreted as a decimal integer,
   optionally multiplied by various values.
   Return -1 on error, 0 on success.

   This supports dd BLOCK size suffixes.
   Note we don't support dd's b=512, c=1, w=2 or 21x512MiB formats.  */
static int
parse_size (char const *str, size_t *size)
{
  uintmax_t tmp_size;
  enum strtol_error e = xstrtoumax (str, NULL, 10, &tmp_size, "EGkKMPTYZ0");
  if (e == LONGINT_OK && tmp_size > SIZE_MAX)
    e = LONGINT_OVERFLOW;

  if (e == LONGINT_OK)
    {
      errno = 0;
      *size = tmp_size;
      return 0;
    }

  errno = (e == LONGINT_OVERFLOW ? EOVERFLOW : 0);
  return -1;
}

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
             program_name);
  else
    {
      printf (_("Usage: %s OPTION... COMMAND\n"), program_name);
      fputs (_("\
Run COMMAND, with modified buffering operations for its standard streams.\n\
\n\
"), stdout);
      fputs (_("\
Mandatory arguments to long options are mandatory for short options too.\n\
"), stdout);
      fputs (_("\
  -i, --input=MODE   Adjust standard input stream buffering\n\
  -o, --output=MODE  Adjust standard output stream buffering\n\
  -e, --error=MODE   Adjust standard error stream buffering\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\n\
If MODE is `L' the corresponding stream will be line buffered.\n\
This option is invalid with standard input.\n"), stdout);
      fputs (_("\n\
If MODE is `0' the corresponding stream will be unbuffered.\n\
"), stdout);
      fputs (_("\n\
Otherwise MODE is a number which may be followed by one of the following:\n\
KB 1000, K 1024, MB 1000*1000, M 1024*1024, and so on for G, T, P, E, Z, Y.\n\
In this case the corresponding stream will be fully buffered with the buffer\n\
size set to MODE bytes.\n\
"), stdout);
      fputs (_("\n\
NOTE: If COMMAND adjusts the buffering of its standard streams (`tee' does\n\
for e.g.) then that will override corresponding settings changed by `stdbuf'.\n\
Also some filters (like `dd' and `cat' etc.) don't use streams for I/O,\n\
and are thus unaffected by `stdbuf' settings.\n\
"), stdout);
      emit_bug_reporting_address ();
    }
  exit (status);
}

/* argv[0] can be anything really, but generally it contains
   the path to the executable or just a name if it was executed
   using $PATH. In the latter case to get the path we can:
   search getenv("PATH"), readlink("/prof/self/exe"), getenv("_"),
   dladdr(), pstat_getpathname(), etc.  */

static void
set_program_path (const char *arg)
{
  if (strchr (arg, '/'))        /* Use absolute or relative paths directly.  */
    {
      program_path = dir_name (arg);
    }
  else
    {
      char *path;
      char tmppath[PATH_MAX + 1];
      ssize_t len = readlink ("/proc/self/exe", tmppath, sizeof (tmppath) - 1);
      if (len > 0)
        {
          tmppath[len] = '\0';
          program_path = dir_name (tmppath);
        }
      else if ((path = getenv ("PATH")))
        {
          char *dir;
          path = xstrdup (path);
          for (dir = strtok (path, ":"); dir != NULL; dir = strtok (NULL, ":"))
            {
              int req = snprintf (tmppath, sizeof (tmppath), "%s/%s", dir, arg);
              if (req >= sizeof (tmppath))
                {
                  error (0, 0, _("path truncated when looking for %s"),
                         quote (arg));
                }
              else if (access (tmppath, X_OK) == 0)
                {
                  program_path = dir_name (tmppath);
                  break;
                }
            }
          free (path);
        }
    }
}

static int
optc_to_fileno (int c)
{
  int ret = -1;

  switch (c)
    {
    case 'e':
      ret = STDERR_FILENO;
      break;
    case 'i':
      ret = STDIN_FILENO;
      break;
    case 'o':
      ret = STDOUT_FILENO;
      break;
    }

  return ret;
}

static void
set_LD_PRELOAD (void)
{
  int ret;
  char *old_libs = getenv ("LD_PRELOAD");
  char *LD_PRELOAD;

  /* Note this would auto add the appropriate search path for "libstdbuf.so":
     gcc stdbuf.c -Wl,-rpath,'$ORIGIN' -Wl,-rpath,$PKGLIBDIR
     However we want the lookup done for the exec'd command not stdbuf.

     Since we don't link against libstdbuf.so add it to LIBDIR rather than
     LIBEXECDIR, as we'll search for it in the "sys default" case below.  */
  char const *const search_path[] = {
    program_path,
    PKGLIBDIR,
    "",                         /* sys default */
    NULL
  };

  char const *const *path = search_path;
  char *libstdbuf;

  do
    {
      struct stat sb;

      if (!**path)              /* system default  */
        {
          libstdbuf = xstrdup (LIB_NAME);
          break;
        }
      ret = asprintf (&libstdbuf, "%s/%s", *path, LIB_NAME);
      if (ret < 0)
        xalloc_die ();
      if (stat (libstdbuf, &sb) == 0)   /* file_exists  */
        break;
      free (libstdbuf);
    }
  while (*++path);

  /* FIXME: Do we need to support libstdbuf.dll, c:, '\' separators etc?  */

  if (old_libs)
    ret = asprintf (&LD_PRELOAD, "LD_PRELOAD=%s:%s", old_libs, libstdbuf);
  else
    ret = asprintf (&LD_PRELOAD, "LD_PRELOAD=%s", libstdbuf);

  if (ret < 0)
    xalloc_die ();

  free (libstdbuf);

  ret = putenv (LD_PRELOAD);

  if (ret != 0)
    {
      error (EXIT_CANCELED, errno,
             _("failed to update the environment with %s"),
             quote (LD_PRELOAD));
    }
}

/* Populate environ with _STDBUF_I=$MODE _STDBUF_O=$MODE _STDBUF_E=$MODE  */

static void
set_libstdbuf_options (void)
{
  int i;

  for (i = 0; i < ARRAY_CARDINALITY (stdbuf); i++)
    {
      if (stdbuf[i].optarg)
        {
          char *var;
          int ret;

          if (*stdbuf[i].optarg == 'L')
            ret = asprintf (&var, "%s%c=L", "_STDBUF_",
                            toupper (stdbuf[i].optc));
          else
            ret = asprintf (&var, "%s%c=%" PRIuMAX, "_STDBUF_",
                            toupper (stdbuf[i].optc),
                            (uintmax_t) stdbuf[i].size);
          if (ret < 0)
            xalloc_die ();

          if (putenv (var) != 0)
            {
              error (EXIT_CANCELED, errno,
                     _("failed to update the environment with %s"),
                     quote (var));
            }
        }
    }
}

int
main (int argc, char **argv)
{
  int c;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  initialize_exit_failure (EXIT_CANCELED);
  atexit (close_stdout);

  while ((c = getopt_long (argc, argv, "+i:o:e:", longopts, NULL)) != -1)
    {
      int opt_fileno;

      switch (c)
        {
        /* Old McDonald had a farm ei...  */
        case 'e':
        case 'i':
        case 'o':
          opt_fileno = optc_to_fileno (c);
          assert (0 <= opt_fileno && opt_fileno < ARRAY_CARDINALITY (stdbuf));
          stdbuf[opt_fileno].optc = c;
          while (c_isspace (*optarg))
            optarg++;
          stdbuf[opt_fileno].optarg = optarg;
          if (c == 'i' && *optarg == 'L')
            {
              /* -oL will be by far the most common use of this utility,
                 but one could easily think -iL might have the same affect,
                 so disallow it as it could be confusing.  */
              error (0, 0, _("line buffering stdin is meaningless"));
              usage (EXIT_CANCELED);
            }

          if (!STREQ (optarg, "L")
              && parse_size (optarg, &stdbuf[opt_fileno].size) == -1)
            error (EXIT_CANCELED, errno, _("invalid mode %s"), quote (optarg));

          break;

        case_GETOPT_HELP_CHAR;

        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          usage (EXIT_CANCELED);
        }
    }

  argv += optind;
  argc -= optind;

  /* must specify at least 1 command.  */
  if (argc < 1)
    {
      error (0, 0, _("missing operand"));
      usage (EXIT_CANCELED);
    }

  /* FIXME: Should we mandate at least one option?  */

  set_libstdbuf_options ();

  /* Try to preload libstdbuf first from the same path as
     stdbuf is running from.  */
  set_program_path (argv[0]);
  if (!program_path)
    program_path = xstrdup (PKGLIBDIR);  /* Need to init to non NULL.  */
  set_LD_PRELOAD ();
  free (program_path);

  execvp (*argv, argv);

  {
    int exit_status = (errno == ENOENT ? EXIT_ENOENT : EXIT_CANNOT_INVOKE);
    error (0, errno, _("failed to run command %s"), quote (argv[0]));
    exit (exit_status);
  }
}
