/*
 * cute-alden - detachable terminal sessions without breaking scrollback
 * Copyright (c) 2025 Matthew Skala
 * $Id: alden.c 12371 2025-06-24 14:39:49Z mskala $
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Matthew Skala
 * http://ansuz.sooke.bc.ca/
 * mskala@ansuz.sooke.bc.ca
 *
 * Modified for the cute-alden fork on 2026-04-21.
 * See the repository README.md and CHANGELOG for downstream changes.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

#define READ_SIZE 4096
#define BUFFER_SIZE 32768
#define MAX_BUFFERS 32
#define PROCSTAT_SIZE 4096

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/**********************************************************************/

/* buffer queues */

typedef struct _BUFFER {
   struct _BUFFER *next;
   int used;
   char data[];
} BUFFER;

BUFFER *free_buf=NULL;

/**********************************************************************/

/* raw terminal I/O */

int tty_fd=-1;

/* try to find the controlling tty of the current process, for options
 * and window size purposes */
int find_tty(void) {
   if (tty_fd<0) {
      if (isatty(STDIN_FILENO))
	tty_fd=STDIN_FILENO;
      else if (isatty(STDOUT_FILENO))
	tty_fd=STDOUT_FILENO;
      else if (isatty(STDERR_FILENO))
	tty_fd=STDERR_FILENO;
      else
	tty_fd=open("/dev/tty",O_RDWR);
   }
   return tty_fd;
}

struct termios saved_termios;
int rawmode_active=0;

void restore_termios(void) {
   if (rawmode_active) {
      tcsetattr(find_tty(),TCSAFLUSH,&saved_termios);
      rawmode_active=0;
   }
}

void set_rawmode(void) {
   struct termios raw_termios;

   tcgetattr(find_tty(),&saved_termios);
   atexit(restore_termios);

   raw_termios=saved_termios;
   raw_termios.c_cflag&=~(CSIZE|PARENB);
   raw_termios.c_cflag|=(CS8);
   raw_termios.c_iflag&=~(BRKINT|ICRNL|IGNBRK|IGNCR|INLCR|INPCK|ISTRIP
			  |IXOFF|IXON|OPOST|PARMRK);
   /* raw_termios.c_iflag|=(IUTF8); probably unnecessary */
   raw_termios.c_lflag&=~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
   raw_termios.c_oflag&=~(OPOST);
   tcsetattr(find_tty(),TCSAFLUSH,&raw_termios);
   rawmode_active=1;
}

/**********************************************************************/

/* detect window size changes */

int check_window=0;

void sigwinch_handler(int sig) {
   if (sig==SIGWINCH) check_window=1;
}

/**********************************************************************/

/* command line options */

static struct option long_opts[] = {
   {"attach",required_argument,NULL,'a'},
   {"allow-nesting",no_argument,NULL,'N'},
   {"buffer-size",required_argument,NULL,'B'},
   {"detach",no_argument,NULL,'d'},
   {"down-buffers",required_argument,NULL,'D'},
   {"help",no_argument,NULL,'h'},
   {"history-bytes",required_argument,NULL,'y'},
   {"list",no_argument,NULL,'L'|128},
   {"login-shell",no_argument,NULL,'l'},
   {"name",required_argument,NULL,'m'},
   {"new-server",no_argument,NULL,'n'},
   {"no-search",no_argument,NULL,'n'},
   {"pid",required_argument,NULL,'i'},
   {"read-size",required_argument,NULL,'R'},
   {"reconnect",no_argument,NULL,'r'},
   {"rename",required_argument,NULL,'Z'},
   {"server-only",no_argument,NULL,'s'},
   {"up-buffers",required_argument,NULL,'U'},
   {"verbose",no_argument,NULL,'v'},
   {"version",no_argument,NULL,'V'},
   {0,0,0,0},
};

int read_size=READ_SIZE,buffer_size=BUFFER_SIZE,
  max_buffers_up=MAX_BUFFERS,max_buffers_down=MAX_BUFFERS;

int allow_nesting=0;
int no_search=0,no_server=0,no_client=0,list_servers=0;
int login_shell=0,verbose=0;
long history_bytes=0;
char *shell_path;
int shell_argc;
char **shell_argv;
pid_t server_pid=-1;
int reconnecting_client=0;
char requested_session_name[128]="";
char active_session_name[128]="";
int started_new_server=0;
int client_used=0;
char rename_session_name[128]="";

enum {
   CLIENT_EXIT_UNKNOWN=0,
   CLIENT_EXIT_DETACHED=1,
   CLIENT_EXIT_CLOSED=2,
};

int client_exit_reason=CLIENT_EXIT_UNKNOWN;

enum {
   MGMT_NONE=0,
   MGMT_DETACH=1,
   MGMT_RENAME=2,
};

int management_mode=MGMT_NONE;

void handle_command_line(int argc,char **argv) {
   int c;
   int show_version=0,show_help=0;
   char *cp;

   if (argc>1) {
      if (!strcmp(argv[1],"detach")) {
	 management_mode=MGMT_DETACH;
	 optind=2;
      } else if ((!strcmp(argv[1],"rename")) && (argc>2)) {
	 management_mode=MGMT_RENAME;
	 strncpy(rename_session_name,argv[2],sizeof(rename_session_name)-1);
	 rename_session_name[sizeof(rename_session_name)-1]=0;
	 optind=3;
      } else if (!strcmp(argv[1],"list")) {
	 list_servers=1;
	 allow_nesting=1;
	 optind=2;
      } else if (!strcmp(argv[1],"help")) {
	 show_help=1;
	 optind=2;
      } else if (!strcmp(argv[1],"version")) {
	 show_version=1;
	 optind=2;
      } else if (argv[1][0]!='-') {
	 strncpy(requested_session_name,argv[1],sizeof(requested_session_name)-1);
	 requested_session_name[sizeof(requested_session_name)-1]=0;
	 no_server=1;
	 optind=2;
      }
   }

   while ((c=getopt_long(argc,argv,"a:B:D:NR:U:Vhi:lm:nrsvy:Z:",
			 long_opts,NULL))!=-1) {
      switch (c) {
       case 'a':
	 strncpy(requested_session_name,optarg,sizeof(requested_session_name)-1);
	 requested_session_name[sizeof(requested_session_name)-1]=0;
	 no_server=1;
	 break;

       case 'B':
	 buffer_size=atol(optarg);
	 if (buffer_size<read_size) {
	    fputs("Buffer size must be at least read size.\n",stderr);
	    buffer_size=read_size;
	 }
	 break;

       case 'D':
	 max_buffers_down=atol(optarg);
	 if ((max_buffers_down==0) || (max_buffers_down==1)) {
	    fputs("Number of downward buffers may not be 0 or 1.\n",stderr);
	    max_buffers_down=2;
	 }
	 break;

       case 'N':
	 allow_nesting=1;
	 break;

       case 'R':
	 read_size=atol(optarg);
	 if (read_size>buffer_size) {
	    fputs("Read size may be at most buffer size.\n",stderr);
	    read_size=buffer_size;
	 }
	 if (read_size<1) {
	    fputs("Read size must be at least 1.\n",stderr);
	    read_size=1;
	 }
	 break;

       case 'U':
	 max_buffers_up=atol(optarg);
	 if ((max_buffers_up==0) || (max_buffers_up==1)) {
	    fputs("Number of upward buffers may not be 0 or 1.\n",stderr);
	    max_buffers_up=2;
	 }
	 break;

       case 'V':
	 show_version=1;
	 break;

       case 'h':
	 show_help=1;
	 break;

       case 'm':
	 strncpy(requested_session_name,optarg,sizeof(requested_session_name)-1);
	 requested_session_name[sizeof(requested_session_name)-1]=0;
	 break;

       case 'y':
	 history_bytes=atol(optarg);
	 if (history_bytes<0) {
	    fputs("History bytes may not be negative.\n",stderr);
	    history_bytes=0;
	 }
	 break;

       case 'Z':
	 management_mode=MGMT_RENAME;
	 strncpy(rename_session_name,optarg,sizeof(rename_session_name)-1);
	 rename_session_name[sizeof(rename_session_name)-1]=0;
	 break;

       case 'i':
	 server_pid=(pid_t)atol(optarg);
	 no_server=1;
	 break;
	 
       case 'n':
	 no_search=1;
	 break;

       case 'r':
	 no_server=1;
	 break;

       case 's':
	 no_client=1;
	 no_search=1;
	 break;

       case 'l':
	 login_shell=1;
	 break;

       case 'v':
	 verbose=1;
	 break;

       case 'L'|128:
	 list_servers=1;
	 allow_nesting=1;
	 break;
	 
       default:
	 break;
      }
   }
   
   if (show_version)
     puts(PACKAGE_STRING "\n\n"
	  "Copyright (C) 2025  Matthew Skala\n"
	  "License GPLv3: GNU GPL version 3 "
	    "<http://gnu.org/licenses/gpl-3.0.html>\n"
	  "This is free software: you are free to change and "
	    "redistribute it.\n"
	  "There is NO WARRANTY, to the extent permitted by law.");

   if (show_help)
     puts("Usage: " PACKAGE_TARNAME " [OPTIONS...] [-- SHELL [SHELLOPTS...]]\n"
	  "       " PACKAGE_TARNAME " detach\n"
	  "       " PACKAGE_TARNAME " list\n"
	  "       " PACKAGE_TARNAME " rename NAME\n"
	  "       " PACKAGE_TARNAME " NAME\n"
	  "Options:\n"
	  "  -a  --attach NAME         reconnect to the named session\n"
	  "  -B  --buffer-size N       number of bytes per buffer (32768)\n"
	  "  -D  --down-buffers N      downward buffers, -1 for unlimited (32)\n"
	  "      --history-bytes N     replay last N output bytes on reconnect\n"
	  "  -N  --allow-nesting       don't refuse nested sessions\n"
	  "  -R  --read-size N         max bytes to read per operation (4096)\n"
	  "      --detach              detach the current/target session client\n"
	  "      --rename NAME         rename the current/target session\n"
	  "  -U  --up-buffers N        upward buffers, -1 for unlimited (32)\n"
	  "  -V  --version             display version and license\n"
	  "  -i  --pid N               reconnect to specified server PID\n"
	  "  -l  --login-shell         open a login shell\n"
	  "  -m  --name NAME           create or reconnect to named session\n"
	  "  -                         bare - is synonym for -l\n"
	  "  -n  --no-search           don't connect to an existing server\n"
	  "      --new-server          synonym for --no-search\n"
	  "  -r  --reconnect           don't start a new server\n"
	  "  -s  --server-only         don't run client (implies --no-search)\n"
	  "  -v  --verbose             give detailed progress messages\n"
	  "      --list                list PIDs of available servers\n"
	  "  --                        args after -- are shell and its args\n"
	  "  -h  --help                display this help\n"
	  "\n"
	  "Bare commands:\n"
	  "  list                      list sessions\n"
	  "  NAME                      attach named session only");

   if (show_version || show_help)
     exit(0);

   if ((optind<argc) &&
       (argv[optind][0]=='-') &&
       (argv[optind][1]==0)) {
      login_shell=1;
      optind++;
   }

   if (management_mode!=MGMT_NONE) {
      shell_argc=0;
      shell_argv=NULL;
      shell_path=NULL;
      return;
   }

   if (optind<argc) {
      shell_argc=argc-optind;
      shell_argv=(char **)malloc(sizeof(char *)*(shell_argc+1));
      for (c=optind;c<argc;c++)
	shell_argv[c-optind]=argv[c];

   } else {
      shell_argc=1;
      shell_argv=(char **)malloc(sizeof(char *)*2);
      shell_argv[0]=getenv("SHELL");
      if (shell_argv[0]==NULL)
	shell_argv[0]="/bin/sh";
   }

   shell_argv[shell_argc]=NULL;
   shell_path=shell_argv[0];

   if (login_shell) {
      shell_argv[0]=(char *)malloc(strlen(shell_path)+2);
      strcpy(shell_argv[0]+1,shell_path);

      shell_argv[0][0]='/';
      for (cp=shell_argv[0]+1;*cp;cp++);
      for (;*cp!='/';cp--);
      *cp='-';
      shell_argv[0]=cp;
   }

   if (verbose) {
      fprintf(stderr,"Shell path:  %s\nShell argc:  %d\n",
	      shell_path,shell_argc);
   }
}

/**********************************************************************/

/* generate filenames for server processes */

uid_t session_uid=(uid_t)-1;
char session_dir[PATH_MAX]="";
char upward_fn[PATH_MAX],downward_fn[PATH_MAX],slavelink_fn[PATH_MAX];
char session_name_fn[PATH_MAX],session_log_fn[PATH_MAX],session_mark_fn[PATH_MAX];
char session_client_fn[PATH_MAX];

void make_filenames(pid_t pid);
int parse_server_pid(const char *path,pid_t *pid_out);
int process_belongs_to_user(pid_t pid);
int validate_session_name(const char *name);
void make_alias_filename(const char *name,char *buf,size_t size);
int read_session_name_for_pid(pid_t pid,char *buf,size_t size);
int alias_pid_for_name(const char *name,pid_t *pid_out);

void fail_errno(const char *message) {
   perror(message);
   exit(1);
}

void fail_message(const char *message) {
   fputs(message,stderr);
   fputc('\n',stderr);
   exit(1);
}

void fail_messagef(const char *format,...) {
   va_list ap;

   va_start(ap,format);
   vfprintf(stderr,format,ap);
   va_end(ap);
   fputc('\n',stderr);
   exit(1);
}

int status_messages_enabled(void) {
   return isatty(STDERR_FILENO);
}

volatile sig_atomic_t requested_client_detach=0;

void sigusr1_handler(int sig) {
   if (sig==SIGUSR1) requested_client_detach=1;
}

void format_session_identity(char *buf,size_t size) {
   if (*active_session_name)
     snprintf(buf,size,"'%s' (pid %ld)",active_session_name,(long)server_pid);
   else
     snprintf(buf,size,"pid %ld",(long)server_pid);
}

void format_reconnect_hint(char *buf,size_t size) {
   if (*active_session_name)
     snprintf(buf,size,"./" PACKAGE_TARNAME " --attach %s",
	      active_session_name);
   else
     snprintf(buf,size,"./" PACKAGE_TARNAME " --pid %ld -r",
	      (long)server_pid);
}

const char *styled_nested_warning(void) {
   if (isatty(STDERR_FILENO))
     return "\033[1;38;2;246;163;200mMay your path be clear.\033[0m";
   return "May your path be clear.";
}

const char *styled_prefix(void) {
   if (isatty(STDERR_FILENO))
     return "\033[38;2;246;163;200m[" PACKAGE_TARNAME "]\033[0m";
   return "[" PACKAGE_TARNAME "]";
}

const char *styled_farewell(void) {
   if (isatty(STDERR_FILENO))
     return "\033[1;38;2;246;163;200mBon voyage !\033[0m";
   return "Bon voyage !";
}

void print_client_status(int disconnecting) {
   char session_buf[256];
   char reconnect_buf[256];
   char latest_name[sizeof(active_session_name)];

   if ((!client_used) || (!status_messages_enabled()))
     return;

   if (rawmode_active)
     restore_termios();

   latest_name[0]=0;
   if (read_session_name_for_pid(server_pid,latest_name,sizeof(latest_name))) {
      strncpy(active_session_name,latest_name,sizeof(active_session_name)-1);
      active_session_name[sizeof(active_session_name)-1]=0;
   }
   format_session_identity(session_buf,sizeof(session_buf));

   if (!disconnecting) {
      if (started_new_server)
	fprintf(stderr,"\r\n%s Started session %s. %s\r\n",
		styled_prefix(),session_buf,styled_farewell());
      else
	fprintf(stderr,"\r\n%s Connected to session %s. %s\r\n",
		styled_prefix(),session_buf,styled_farewell());
      return;
   }

   if (client_exit_reason==CLIENT_EXIT_CLOSED) {
      fprintf(stderr,"\r\n%s Session %s closed. %s\r\n",
	      styled_prefix(),session_buf,styled_farewell());
      return;
   }

   format_reconnect_hint(reconnect_buf,sizeof(reconnect_buf));
   fprintf(stderr,
	   "\r\n%s Detached from session %s. %s\r\n%s Reconnect with: %s\r\n",
	   styled_prefix(),session_buf,styled_farewell(),
	   styled_prefix(),reconnect_buf);
}

int current_session_pid(pid_t *pid_out) {
   pid_t env_pid;
   int test_pid,test_fd,i;
   char procstat_fn[64],procstat_buf[PROCSTAT_SIZE];

   if (getenv("CUTE_ALDEN_SESSION_PID"))
      env_pid=(pid_t)atol(getenv("CUTE_ALDEN_SESSION_PID"));
   else if (getenv("ALDEN_SESSION_PID"))
      env_pid=(pid_t)atol(getenv("ALDEN_SESSION_PID"));
   else
      env_pid=-1;

   if (env_pid>1) {
      if (process_belongs_to_user(env_pid)) {
	 *pid_out=env_pid;
	 return 1;
      }
   }

   test_pid=getpid();
   while (test_pid>1) {
      make_filenames(test_pid);
      test_fd=open(downward_fn,O_PATH);
      if (test_fd>=0) {
	 close(test_fd);
	 *pid_out=test_pid;
	 return 1;
      }

      snprintf(procstat_fn,64,"/proc/%d/stat",test_pid);
      test_fd=open(procstat_fn,O_RDONLY);
      if (test_fd<0)
	break;

      i=read(test_fd,procstat_buf,PROCSTAT_SIZE);
      close(test_fd);
      if (i<=0)
	break;

      procstat_buf[0]=')';
      for (i--; (i>0) && (procstat_buf[i]!=')'); i--);
      if (i<=0)
	break;
      i+=4;

      for (test_pid=0;procstat_buf[i]!=' ';i++) {
	 test_pid*=10;
	 test_pid+=(procstat_buf[i]-'0');
      }
   }

   return 0;
}

void write_client_pid(void) {
   int fd;
   char buf[32];

   fd=open(session_client_fn,O_WRONLY|O_CREAT|O_TRUNC,0600);
   if (fd<0)
     return;

   snprintf(buf,sizeof(buf),"%ld\n",(long)getpid());
   write(fd,buf,strlen(buf));
   close(fd);
}

pid_t read_client_pid(void) {
   int fd,i;
   char buf[32];

   fd=open(session_client_fn,O_RDONLY|O_NOFOLLOW);
   if (fd<0)
     return -1;

   i=read(fd,buf,sizeof(buf)-1);
   close(fd);
   if (i<=0)
     return -1;

   buf[i]=0;
   return (pid_t)atol(buf);
}

void clear_client_pid_if_current(void) {
   pid_t pid;

   pid=read_client_pid();
   if (pid==(pid_t)getpid())
     unlink(session_client_fn);
}

int resolve_management_target(void) {
   pid_t target_pid;

   if (server_pid>1) {
      if (!process_belongs_to_user(server_pid))
	return 0;
      target_pid=server_pid;

   } else if (*requested_session_name) {
      if (!alias_pid_for_name(requested_session_name,&target_pid))
	return 0;
      if (!process_belongs_to_user(target_pid))
	return 0;

   } else {
      if (!current_session_pid(&target_pid))
	return 0;
      if (!process_belongs_to_user(target_pid))
	return 0;
   }

   server_pid=target_pid;
   make_filenames(server_pid);
   read_session_name_for_pid(server_pid,active_session_name,
			     sizeof(active_session_name));
   return 1;
}

void rename_target_session(void) {
   char old_name[sizeof(active_session_name)];
   char old_alias[PATH_MAX],new_alias[PATH_MAX];
   char alias_target[64];
   int name_fd;
   pid_t existing_pid;

   if (!validate_session_name(rename_session_name))
     fail_message("New session name may only use letters, digits, '.', '-', and '_'.");

   if (!resolve_management_target())
     fail_message("No target session to rename.");

   old_name[0]=0;
   read_session_name_for_pid(server_pid,old_name,sizeof(old_name));
   if (*old_name && !strcmp(old_name,rename_session_name))
     return;

   make_alias_filename(rename_session_name,new_alias,sizeof(new_alias));
   if (alias_pid_for_name(rename_session_name,&existing_pid) &&
       process_belongs_to_user(existing_pid))
     fail_messagef("Session name '%s' is already in use.",
		   rename_session_name);
   unlink(new_alias);

   name_fd=open(session_name_fn,O_WRONLY|O_CREAT|O_TRUNC,0600);
   if (name_fd<0)
     fail_errno("Could not update session name file");
   write(name_fd,rename_session_name,strlen(rename_session_name));
   write(name_fd,"\n",1);
   close(name_fd);

   snprintf(alias_target,sizeof(alias_target),"%ld.d",(long)server_pid);
   if (symlink(alias_target,new_alias)<0)
     fail_errno("Could not create session alias");

   if (*old_name) {
      make_alias_filename(old_name,old_alias,sizeof(old_alias));
      unlink(old_alias);
   }

   strncpy(active_session_name,rename_session_name,sizeof(active_session_name)-1);
   active_session_name[sizeof(active_session_name)-1]=0;
}

void detach_target_session(void) {
   pid_t client_pid;

   if (!resolve_management_target())
     fail_message("No target session to detach.");

   client_pid=read_client_pid();
   if (client_pid<=1)
     fail_message("Session has no attached client.");

   if (kill(client_pid,SIGUSR1)<0)
     fail_errno("Could not signal session client");
}

void ensure_session_dir(void) {
   struct stat st;
   int needed;

   if (*session_dir)
     return;

   session_uid=geteuid();
   needed=snprintf(session_dir,sizeof(session_dir),
		   "/tmp/" PACKAGE_TARNAME "-%lu",
		   (unsigned long)session_uid);
   if ((needed<0) || ((size_t)needed>=sizeof(session_dir)))
     fail_message("Session directory path is too long.");

   if (mkdir(session_dir,0700)<0) {
      if (errno!=EEXIST)
	fail_errno("Could not create session directory");
   }

   if (lstat(session_dir,&st)<0)
     fail_errno("Could not stat session directory");

   if (!S_ISDIR(st.st_mode))
     fail_message("Session directory path is not a directory.");

   if (st.st_uid!=session_uid)
     fail_message("Session directory is not owned by the current user.");

   if ((st.st_mode&077)!=0) {
      if (chmod(session_dir,0700)<0)
	fail_errno("Could not secure session directory permissions");

      if (lstat(session_dir,&st)<0)
	fail_errno("Could not restat session directory");

      if ((st.st_mode&077)!=0)
	fail_message("Session directory permissions are too broad.");
   }
}

int owned_private_fifo(const char *path) {
   struct stat st;

   if (lstat(path,&st)<0)
     return 0;

   if (!S_ISFIFO(st.st_mode))
     return 0;

   if (st.st_uid!=session_uid)
     return 0;

   if ((st.st_mode&077)!=0)
     return 0;

   return 1;
}

int owned_session_link(const char *path) {
   struct stat st;

   if (lstat(path,&st)<0)
     return 0;

   if (!S_ISLNK(st.st_mode))
     return 0;

   if (st.st_uid!=session_uid)
     return 0;

   return 1;
}

int process_belongs_to_user(pid_t pid) {
   char proc_fn[64];
   char procstat_buf[PROCSTAT_SIZE];
   struct stat st;
   int proc_fd,i;

   if (pid<=1)
     return 0;

   snprintf(proc_fn,sizeof(proc_fn),"/proc/%ld",(long)pid);
   if (stat(proc_fn,&st)<0)
     return 0;

   if (st.st_uid!=session_uid)
     return 0;

   snprintf(proc_fn,sizeof(proc_fn),"/proc/%ld/stat",(long)pid);
   proc_fd=open(proc_fn,O_RDONLY);
   if (proc_fd<0)
     return 0;

   i=read(proc_fd,procstat_buf,sizeof(procstat_buf));
   close(proc_fd);
   if (i<=0)
     return 0;

   procstat_buf[0]=')'; /* sentinel */
   for (i--; (i>0) && (procstat_buf[i]!=')'); i--);
   if (i<=0)
     return 0;

   i+=2; /* skip end-paren and following space */
   if ((procstat_buf[i]=='Z') || (procstat_buf[i]=='X'))
     return 0;

   if (kill(pid,0)<0)
     return 0;

   return 1;
}

int validate_session_name(const char *name) {
   int i;

   if ((name==NULL) || (!*name))
     return 0;

   for (i=0;name[i];i++) {
      if (!(((name[i]>='a') && (name[i]<='z')) ||
	    ((name[i]>='A') && (name[i]<='Z')) ||
	    ((name[i]>='0') && (name[i]<='9')) ||
	    (name[i]=='-') || (name[i]=='_') ||
	    (name[i]=='.')))
	return 0;
   }

   return 1;
}

void validate_requested_session_name(void) {
   if (*requested_session_name && !validate_session_name(requested_session_name))
     fail_message("Session name may only use letters, digits, '.', '-', and '_'.");
}

void make_alias_filename(const char *name,char *buf,size_t size) {
   int needed;

   ensure_session_dir();
   needed=snprintf(buf,size,"%s/name.%s",session_dir,name);
   if ((needed<0) || ((size_t)needed>=size))
     fail_message("Session alias path is too long.");
}

int read_session_name_for_pid(pid_t pid,char *buf,size_t size) {
   int fd,i;

   make_filenames(pid);
   fd=open(session_name_fn,O_RDONLY|O_NOFOLLOW);
   if (fd<0)
     return 0;

   i=read(fd,buf,size-1);
   close(fd);
   if (i<=0)
     return 0;

   buf[i]=0;
   while ((i>0) && ((buf[i-1]=='\n') || (buf[i-1]=='\r'))) {
      buf[i-1]=0;
      i--;
   }

   return validate_session_name(buf);
}

int alias_pid_for_name(const char *name,pid_t *pid_out) {
   char alias_fn[PATH_MAX];
   char target[PATH_MAX];
   int i;

   make_alias_filename(name,alias_fn,sizeof(alias_fn));
   i=readlink(alias_fn,target,sizeof(target)-1);
   if (i<0)
     return 0;

   target[i]=0;
   return parse_server_pid(target,pid_out);
}

void trim_history_log(int log_fd) {
   (void)log_fd;
}

void write_history_mark(long mark) {
   int fd;
   char buf[32];

   fd=open(session_mark_fn,O_WRONLY|O_CREAT|O_TRUNC,0600);
   if (fd<0)
     return;

   snprintf(buf,sizeof(buf),"%ld\n",mark);
   write(fd,buf,strlen(buf));
   close(fd);
}

long read_history_mark(void) {
   int fd,i;
   char buf[32];

   fd=open(session_mark_fn,O_RDONLY|O_NOFOLLOW);
   if (fd<0)
     return 0;

   i=read(fd,buf,sizeof(buf)-1);
   close(fd);
   if (i<=0)
     return 0;

   buf[i]=0;
   return atol(buf);
}

void replay_history_file(void) {
   int log_fd,i;
   char buf[READ_SIZE];
   long mark,start;
   off_t size;

   if ((!reconnecting_client) || (history_bytes<=0))
     return;

   mark=read_history_mark();
   if (mark<=0)
     return;

   log_fd=open(session_log_fn,O_RDONLY|O_NOFOLLOW);
   if (log_fd<0)
     return;

   size=lseek(log_fd,0,SEEK_END);
   if (size<0) {
      close(log_fd);
      return;
   }

   if (mark>size)
     mark=size;

   start=mark-history_bytes;
   if (start<0)
     start=0;

   if (lseek(log_fd,start,SEEK_SET)<0) {
      close(log_fd);
      return;
   }

   while (start<mark) {
      size_t want;

      want=mark-start;
      if (want>sizeof(buf))
	want=sizeof(buf);

      i=read(log_fd,buf,want);
      if (i<=0)
	break;

      write(STDOUT_FILENO,buf,i);
      start+=i;
   }

   close(log_fd);
}

int parse_server_pid(const char *path,pid_t *pid_out) {
   char *endptr;
   const char *base;
   long parsed;

   base=strrchr(path,'/');
   if (base)
     base++;
   else
     base=path;

   errno=0;
   parsed=strtol(base,&endptr,10);
   if ((errno!=0) || (endptr==base) || strcmp(endptr,".d"))
     return 0;

   if ((parsed<=1) || (parsed>INT_MAX))
     return 0;

   *pid_out=(pid_t)parsed;
   return 1;
}

void cleanup_session_files(pid_t pid) {
   char stale_name[sizeof(active_session_name)];
   char alias_fn[PATH_MAX];

   stale_name[0]=0;
   read_session_name_for_pid(pid,stale_name,sizeof(stale_name));
   make_filenames(pid);
   unlink(session_log_fn);
   unlink(session_mark_fn);
   unlink(session_client_fn);
   unlink(session_name_fn);
   if (*stale_name) {
      make_alias_filename(stale_name,alias_fn,sizeof(alias_fn));
      unlink(alias_fn);
   }
   unlink(slavelink_fn);
   unlink(upward_fn);
   unlink(downward_fn);
}

void make_filenames(pid_t pid) {
   int needed;

   ensure_session_dir();

   needed=snprintf(downward_fn,sizeof(downward_fn),
		   "%s/%ld.d",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(downward_fn)))
     fail_message("Downward FIFO path is too long.");

   needed=snprintf(upward_fn,sizeof(upward_fn),
		   "%s/%ld.u",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(upward_fn)))
     fail_message("Upward FIFO path is too long.");

   needed=snprintf(slavelink_fn,sizeof(slavelink_fn),
		   "%s/%ld.s",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(slavelink_fn)))
     fail_message("Slave PTY link path is too long.");

   needed=snprintf(session_name_fn,sizeof(session_name_fn),
		   "%s/%ld.name",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(session_name_fn)))
     fail_message("Session name path is too long.");

   needed=snprintf(session_log_fn,sizeof(session_log_fn),
		   "%s/%ld.log",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(session_log_fn)))
     fail_message("Session log path is too long.");

   needed=snprintf(session_mark_fn,sizeof(session_mark_fn),
		   "%s/%ld.mark",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(session_mark_fn)))
     fail_message("Session mark path is too long.");

   needed=snprintf(session_client_fn,sizeof(session_client_fn),
		   "%s/%ld.client",session_dir,(long)pid);
   if ((needed<0) || ((size_t)needed>=sizeof(session_client_fn)))
     fail_message("Session client path is too long.");
}

/**********************************************************************/

/* prevent nesting */

void prevent_nesting(void) {
   int test_pid,test_fd,i;
   char procstat_fn[64],procstat_buf[PROCSTAT_SIZE];

   if (verbose)
     fputs("Checking ancestors to exclude nested session.\n",stderr);

   test_pid=getpid();
   if (verbose)
     fprintf(stderr,"We are PID %d.\n",test_pid);

   while (test_pid>1) {
      make_filenames(test_pid);

      test_fd=open(downward_fn,O_PATH);
      if (test_fd>=0) {
	 if (verbose)
	   fprintf(stderr,"...found a pipe at %s.\n",downward_fn);

	 fprintf(stderr,"Nested session rejected.  %s\n",
		 styled_nested_warning());
	 exit(1);
      }

      snprintf(procstat_fn,64,"/proc/%d/stat",test_pid);
      test_fd=open(procstat_fn,O_RDONLY);
      if (test_fd<0) {
	 if (verbose)
	   fprintf(stderr,"Error opening %s, assuming we're okay.\n",
		   procstat_fn);
	 break;
      }

      i=read(test_fd,procstat_buf,PROCSTAT_SIZE);
      if (i>0) {
	 /* because parentheses and spaces are allowed in executable
	  * filenames, the only reliable way to parse /proc/PID/stat
	  * contents is to search for the *last* end-paren character
	  * and use that as the marker for the end of field 2, after
	  * which other fields are space separated, and expected not
	  * to contain parentheses.
	  */

	 procstat_buf[0]=')'; /* sentinel */
	 for (i--;procstat_buf[i]!=')';i--); /* find last end-paren */
	 i+=4; /* skip end-paren, space, one-char state field, space */

	 for (test_pid=0;procstat_buf[i]!=' ';i++) {
	    test_pid*=10;
	    test_pid+=(procstat_buf[i]-'0');
	 }

	 if (verbose)
	   fprintf(stderr,"...parent is %d\n",test_pid);

      } else {
	 if (verbose)
	   fprintf(stderr,"Error reading %s, assuming we're okay.\n",
		   procstat_fn);
	test_pid=-1;
      }

      close(test_fd);
   }
}

/**********************************************************************/

/* search for a server */

int have_server=0;
int up_fd=-1,down_fd=-1,slave_fd=-1;

void search_for_server(void) {
   char gpattern[PATH_MAX];
   glob_t globbuf;
   size_t i;
   pid_t candidate_pid;
   char candidate_name[sizeof(active_session_name)];

   active_session_name[0]=0;
   reconnecting_client=0;
   started_new_server=0;

   if (*requested_session_name) {
      if (alias_pid_for_name(requested_session_name,&candidate_pid)) {
	 char alias_fn[PATH_MAX];

	 make_alias_filename(requested_session_name,alias_fn,sizeof(alias_fn));
	 make_filenames(candidate_pid);
	 if (!process_belongs_to_user(candidate_pid)) {
	    cleanup_session_files(candidate_pid);
	    unlink(alias_fn);
	    return;
	 }

	 server_pid=candidate_pid;
	 strncpy(active_session_name,requested_session_name,
		 sizeof(active_session_name)-1);
	 active_session_name[sizeof(active_session_name)-1]=0;

	 down_fd=open(downward_fn,O_RDWR|O_NOFOLLOW);
	 if (down_fd<0)
	   return;
	 if (flock(down_fd,LOCK_EX|LOCK_NB)<0) {
	    close(down_fd);
	    return;
	 }

	 up_fd=open(upward_fn,O_RDWR|O_NOFOLLOW);
	 if (up_fd<0) {
	    flock(down_fd,LOCK_UN);
	    close(down_fd);
	    return;
	 }
	 if (flock(up_fd,LOCK_EX|LOCK_NB)<0) {
	    close(up_fd);
	    flock(down_fd,LOCK_UN);
	    close(down_fd);
	    return;
	 }

	 if (owned_session_link(slavelink_fn))
	   slave_fd=open(slavelink_fn,O_RDWR);
	 else
	   slave_fd=-1;

	 have_server=1;
	 reconnecting_client=1;
      }
      return;
   }

   if (server_pid<0) {
      ensure_session_dir();
      if ((size_t)snprintf(gpattern,sizeof(gpattern),"%s/*.d",session_dir)
	  >=sizeof(gpattern))
	fail_message("Search glob pattern is too long.");

   } else {
      make_filenames(server_pid);
      if ((size_t)snprintf(gpattern,sizeof(gpattern),"%s",downward_fn)
	  >=sizeof(gpattern))
	fail_message("Search glob pattern is too long.");
   }

   if (verbose)
     fprintf(stderr,"Searching for a server, glob pattern %s\n",gpattern);

   globbuf.gl_offs=0;
   glob(gpattern,0,NULL,&globbuf);

   if (verbose && (globbuf.gl_pathc==0))
       fputs("Nothing found.\n",stderr);
   
   have_server=0;
   for (i=0;i<globbuf.gl_pathc;i++) {
      if (!parse_server_pid(globbuf.gl_pathv[i],&candidate_pid)) {
	 if (verbose)
	   fprintf(stderr,"Skipping malformed candidate %s.\n",
		   globbuf.gl_pathv[i]);
	 continue;
      }

      make_filenames(candidate_pid);
      candidate_name[0]=0;
      read_session_name_for_pid(candidate_pid,candidate_name,
				sizeof(candidate_name));
      if (!owned_private_fifo(downward_fn)) {
	 if (verbose)
	   fprintf(stderr,"Skipping untrusted FIFO %s.\n",downward_fn);
	 continue;
      }

      if (verbose)
	fprintf(stderr,"Checking %s.\n",downward_fn);

      down_fd=open(downward_fn,O_RDWR|O_NOFOLLOW);
      if (down_fd<0) {
	 if (verbose)
	   fputs("...can't open.\n",stderr);
	 continue;
      }

      if (flock(down_fd,LOCK_EX|LOCK_NB)<0) {
	 close(down_fd);
	 if (verbose)
	   fputs("...can't acquire lock.\n",stderr);
	 continue;
      }

      if (!owned_private_fifo(upward_fn)) {
	 flock(down_fd,LOCK_UN);
	 close(down_fd);
	 if (verbose)
	   fprintf(stderr,"Skipping untrusted FIFO %s.\n",upward_fn);
	 continue;
      }

      if (verbose)
	fprintf(stderr,"Checking %s.\n",upward_fn);

      up_fd=open(upward_fn,O_RDWR|O_NOFOLLOW);
      if (up_fd<0) {
	 flock(down_fd,LOCK_UN);
	 close(down_fd);
	 if (verbose)
	   fputs("...can't open.\n",stderr);
	 continue;
      }
   
      if (flock(up_fd,LOCK_EX|LOCK_NB)<0) {
	 close(up_fd);
	 flock(down_fd,LOCK_UN);
	 close(down_fd);
	 if (verbose)
	   fputs("...can't acquire lock.\n",stderr);
	 continue;
      }

      server_pid=candidate_pid;
      if (!process_belongs_to_user(server_pid)) {
	 if (verbose)
	   fprintf(stderr,
		   "...process %d is stale or belongs to another user, deleting stale files.\n",
		  server_pid);

	 cleanup_session_files(server_pid);
	 flock(up_fd,LOCK_UN);
	 close(up_fd);
	 flock(down_fd,LOCK_UN);
	 close(down_fd);
	 continue;
      }

      if (owned_session_link(slavelink_fn))
	slave_fd=open(slavelink_fn,O_RDWR);
      else
	slave_fd=-1;

      if (list_servers) {
	 if (*candidate_name)
	   printf("%d\t%s\n",server_pid,candidate_name);
	 else
	   printf("%d\t-\n",server_pid);
	 flock(up_fd,LOCK_UN);
	 close(up_fd);
	 flock(down_fd,LOCK_UN);
	 close(down_fd);
	 continue;
      }
      
      if (verbose)
	fputs("Server found.\n",stderr);
      
      strncpy(active_session_name,candidate_name,sizeof(active_session_name)-1);
      active_session_name[sizeof(active_session_name)-1]=0;
      have_server=1;
      reconnecting_client=1;
      break;
   }

   globfree(&globbuf);

   if (list_servers)
     exit(0);
   
   if (verbose && !have_server)
     fputs("No usable server found.\n",stderr);
}

/**********************************************************************/

/* run a server */

void start_server(void) {
   int i;
   int pipefd[2],readyfd[2];
   struct winsize window_size;

   if (verbose)
     fputs("Starting a new server.\n",stderr);
   
   ioctl(find_tty(),TIOCGWINSZ,&window_size);
   
#ifdef HAVE_PIPE2
   pipe2(pipefd,0);
   pipe2(readyfd,0);
#else
   pipe(pipefd);
   pipe(readyfd);
#endif

   if (*requested_session_name) {
      strncpy(active_session_name,requested_session_name,
	      sizeof(active_session_name)-1);
      active_session_name[sizeof(active_session_name)-1]=0;
   } else
     active_session_name[0]=0;

   server_pid=fork();
   
   if (server_pid==0) {
      /* SERVER PROCESS */

      int shell_pid;
      struct pollfd poll_fds[3];
      int up_bufcnt=0,down_bufcnt=0;
      BUFFER *up_head_buf=NULL,*down_head_buf=NULL;
      BUFFER *up_tail_buf=NULL,*down_tail_buf=NULL;
      int up_chars=0,down_chars=0;
      BUFFER *tbp;
      int timeout_ms;
      int master_fd;
      char *slave_fn;
      struct sigaction sa={0};
      int log_fd=-1;
      char pidbuf[32];

      server_pid=getpid();
      make_filenames(server_pid);
      
      /* wait for parent to tell us it has created up/down files */
      close(pipefd[1]);
      close(readyfd[0]);
      read(pipefd[0],&i,sizeof(i));
      close(pipefd[0]);

      down_fd=open(downward_fn,O_RDWR|O_NOFOLLOW);
      up_fd=open(upward_fn,O_RDWR|O_NOFOLLOW);

      master_fd=posix_openpt(O_RDWR);
      if (master_fd>=0) {
	 if (history_bytes>0) {
	    log_fd=open(session_log_fn,O_RDWR|O_CREAT|O_TRUNC,0600);
	    if (log_fd<0)
	      fail_errno("Could not create session log");
	 }

	 grantpt(master_fd);
	 unlockpt(master_fd);
	 
	 slave_fn=ptsname(master_fd);

	 /* we must create slave link - client hopes to pick it up later */
	 unlink(slavelink_fn);
	 if ((slave_fn!=NULL) &&
	     (symlink(slave_fn,slavelink_fn)<0) &&
	     (errno!=EEXIST) && verbose)
	   perror("Could not create slave PTY link");

	 shell_pid=fork();
	 if (shell_pid==0) {
	    /* SHELL PROCESS */

	    close(master_fd);
	    close(up_fd);
	    close(down_fd);

	    setsid();
	    slave_fd=open(slave_fn,O_RDWR);
	    if (slave_fd<0) {
	       if (verbose)
		 perror("Shell process could not open slave PTY");
	       else
		 fputs("Shell process could not open slave PTY.\n",stderr);
	       exit(1);
	    }

	    ioctl(slave_fd,TIOCSCTTY,0);
	    ioctl(slave_fd,TIOCSWINSZ,&window_size);

	    dup2(slave_fd,STDIN_FILENO);
	    dup2(slave_fd,STDOUT_FILENO);
	    dup2(slave_fd,STDERR_FILENO);
	    if (slave_fd>STDERR_FILENO)
	      close(slave_fd);

	    snprintf(pidbuf,sizeof(pidbuf),"%ld",(long)server_pid);
	    setenv("CUTE_ALDEN","1",1);
	    setenv("CUTE_ALDEN_SESSION_ACTIVE","1",1);
	    setenv("CUTE_ALDEN_SESSION_PID",pidbuf,1);
	    if (*active_session_name)
	      setenv("CUTE_ALDEN_SESSION_NAME",active_session_name,1);
	    else
	      unsetenv("CUTE_ALDEN_SESSION_NAME");
	    if (history_bytes>0)
	      setenv("CUTE_ALDEN_SESSION_LOG",session_log_fn,1);
	    else
	      unsetenv("CUTE_ALDEN_SESSION_LOG");

	    setenv("ALDEN","1",1);
	    setenv("ALDEN_SESSION_ACTIVE","1",1);
	    setenv("ALDEN_SESSION_PID",pidbuf,1);
	    if (*active_session_name)
	      setenv("ALDEN_SESSION_NAME",active_session_name,1);
	    else
	      unsetenv("ALDEN_SESSION_NAME");
	    if (history_bytes>0)
	      setenv("ALDEN_SESSION_LOG",session_log_fn,1);
	    else
	      unsetenv("ALDEN_SESSION_LOG");

	    execvp(shell_path,shell_argv);

	    /* there should be no return from execvp */

	    if (verbose)
	      fputs("Server's exec of shell failed.  "
		      "With myself alone I was angry,\n"
		    "seeing how badly I managed "
		      "the matter I had in my keeping.\n",
		    stderr);
	    else
	      fputs("Server's exec of shell failed.\n",stderr);

	    exit(1);

	    /* END OF SHELL PROCESS */
	 }

	 fcntl(master_fd,F_SETFL,O_RDWR|O_NONBLOCK);
	 fcntl(down_fd,F_SETFL,O_RDWR|O_NONBLOCK);
	 fcntl(up_fd,F_SETFL,O_WRONLY|O_NONBLOCK);

	 sa.sa_handler=SIG_IGN;
	 sigaction(SIGHUP,&sa,NULL);
	 sigaction(SIGPIPE,&sa,NULL);

	 i=0x9f41a332;
	 write(readyfd[1],&i,sizeof(i));
	 close(readyfd[1]);
	    
	 timeout_ms=1;
	 while (waitpid(shell_pid,NULL,WNOHANG)==0) {
	    poll_fds[0].fd=master_fd;
	    if (down_head_buf)
	      poll_fds[0].events=POLLIN|POLLOUT;
	    else
	      poll_fds[0].events=POLLIN;
	    poll_fds[0].revents=0;

	    if ((down_bufcnt<max_buffers_down) || (max_buffers_down<0)) {
	       poll_fds[1].fd=down_fd;
	       poll_fds[1].events=POLLIN;
	    } else {
	       poll_fds[1].fd=~down_fd;
	       poll_fds[1].events=0;
	    }
	    poll_fds[1].revents=0;

	    if (up_fd<0) {
	       poll_fds[2].fd=up_fd;
	       poll_fds[2].events=0;
	    } else if (up_head_buf) {
	       poll_fds[2].fd=up_fd;
	       poll_fds[2].events=POLLOUT;
	    } else {
	       poll_fds[2].fd=~up_fd;
	       poll_fds[2].events=0;
	    }
	    poll_fds[2].revents=0;

	    if (poll(poll_fds,3,timeout_ms)==0) {
	       timeout_ms*=3;
	       if (timeout_ms>5000)
		 timeout_ms=5000;
	    } else
	      timeout_ms=1;

	    if (poll_fds[0].revents&(POLLERR|POLLHUP)) {
	       break;

	    } else {
	       if (poll_fds[0].revents&POLLIN) {
		  if (up_head_buf==NULL) {
		     /* no queue, start one */
		     if (free_buf) {
			up_head_buf=free_buf;
			free_buf=free_buf->next;
		     } else {
			up_head_buf=
			  (BUFFER *)malloc(sizeof(BUFFER)+buffer_size);
		     }
		     up_head_buf->next=NULL;
		     up_head_buf->used=0;
		     up_tail_buf=up_head_buf;
		     up_bufcnt=1;
		  }
		     
		  if (up_tail_buf->used+read_size>=buffer_size) {
		     /* read will not fit in buffer, need a new one */

		     if ((up_bufcnt>=max_buffers_up) && (max_buffers_up>0)) {
			/* cannot make queue any longer, drop some data! */
			tbp=up_head_buf;
			up_head_buf=tbp->next;
			up_bufcnt--;
			up_chars=0;
			tbp->next=free_buf;
			free_buf=tbp;
		     }
			
		     /* add a buffer at the tail */
		     if (free_buf) {
			up_tail_buf->next=free_buf;
			free_buf=free_buf->next;
		     } else {
			up_tail_buf->next=
			  (BUFFER *)malloc(sizeof(BUFFER)+buffer_size);
		     }
		     up_tail_buf=up_tail_buf->next;
		     up_tail_buf->next=NULL;
		     up_tail_buf->used=0;
		     up_bufcnt++;
		  }
		     
		  i=read(master_fd,up_tail_buf->data+up_tail_buf->used,
			 READ_SIZE);
		  if (i>0) {
		     up_tail_buf->used+=i;
		     if (log_fd>=0) {
			write(log_fd,up_tail_buf->data+up_tail_buf->used-i,i);
			trim_history_log(log_fd);
		     }
		  }
	       }
	       
	       if (down_head_buf) {
		  i=down_head_buf->used-down_chars;
		  if (i>READ_SIZE) i=READ_SIZE;
		  i=write(master_fd,down_head_buf->data+down_chars,i);
		  if (i>0) down_chars+=i;
		  
		  if (down_chars>=down_head_buf->used) {
		     down_chars=0;
		     
		     tbp=down_head_buf;
		     down_head_buf=tbp->next;
		     if (down_head_buf==NULL)
		       down_tail_buf=NULL;
		     down_bufcnt--;
			
		     tbp->next=free_buf;
		     tbp->used=0;
		     free_buf=tbp;
		  }
	       }
	    }

	    if (poll_fds[1].revents&(POLLERR|POLLHUP)) {
	       close(down_fd);
	       down_fd=open(downward_fn,O_RDWR|O_NONBLOCK);

	    } else if (poll_fds[1].revents&POLLIN) {
	       if (down_head_buf==NULL) {
		  /* no queue, start one */
		  if (free_buf) {
		     down_head_buf=free_buf;
		     free_buf=free_buf->next;
		  } else {
		     down_head_buf=
		       (BUFFER *)malloc(sizeof(BUFFER)+buffer_size);
		  }
		  down_head_buf->next=NULL;
		  down_head_buf->used=0;
		  down_tail_buf=down_head_buf;
		  down_bufcnt=1;
	       }

	       if (down_tail_buf->used+read_size>=buffer_size) {
		  /* read will not fit in buffer, need a new one */

		  /* enforcement of max buffers in this direction is done
		   * by choking reads; we still buffer everything we read */

		  /* add a buffer at the tail */
		  if (free_buf) {
		     down_tail_buf->next=free_buf;
		     free_buf=free_buf->next;
		  } else {
		     down_tail_buf->next=
		       (BUFFER *)malloc(sizeof(BUFFER)+buffer_size);
		  }
		  down_tail_buf=down_tail_buf->next;
		  down_tail_buf->next=NULL;
		  down_tail_buf->used=0;
		  down_bufcnt++;
	       }

	       i=read(down_fd,down_tail_buf->data+down_tail_buf->used,
		      READ_SIZE);
	       if (i>0) down_tail_buf->used+=i;
	    }

	    if (poll_fds[2].revents&(POLLERR|POLLHUP)) {
	       if (log_fd>=0) {
		  long mark;

		  mark=lseek(log_fd,0,SEEK_END);
		  if (mark>=0)
		    write_history_mark(mark);
	       }
	       close(up_fd);
	       up_fd=open(upward_fn,O_RDWR|O_NONBLOCK);
	       
	    } else if (up_head_buf) {
	       i=up_head_buf->used-up_chars;
	       if (i>READ_SIZE) i=READ_SIZE;
	       i=write(up_fd,up_head_buf->data+up_chars,i);
	       if (i>0) up_chars+=i;
	       
	       if (up_chars>=up_head_buf->used) {
		  up_chars=0;
		  
		  tbp=up_head_buf;
		  up_head_buf=tbp->next;
		  if (up_head_buf==NULL)
		    up_tail_buf=NULL;
		  up_bufcnt--;
		  
		  tbp->next=free_buf;
		  tbp->used=0;
		  free_buf=tbp;
	       }
	    }
	 }
      }

      close(readyfd[1]);

      if (log_fd>=0)
	close(log_fd);
      cleanup_session_files(server_pid);
      close(master_fd);
      close(up_fd);
      close(down_fd);
      
      exit(0);

      /* END OF SERVER PROCESS */
   }

   make_filenames(server_pid);
   reconnecting_client=0;
   cleanup_session_files(server_pid);
   close(pipefd[0]);
   close(readyfd[1]);

   if (mkfifo(downward_fn,0600)<0)
     fail_errno("Could not create downward FIFO");
   down_fd=open(downward_fn,O_RDWR|O_NOFOLLOW);
   if (down_fd<0)
     fail_errno("Could not open downward FIFO");
   flock(down_fd,LOCK_EX); /* must lock this one before creating other */

   if (mkfifo(upward_fn,0600)<0)
     fail_errno("Could not create upward FIFO");
   up_fd=open(upward_fn,O_RDWR|O_NOFOLLOW);
   if (up_fd<0)
     fail_errno("Could not open upward FIFO");
   flock(up_fd,LOCK_EX);

   slave_fd=-1;

   i=0x2b00b1e5;
   write(pipefd[1],&i,sizeof(i));
   close(pipefd[1]);
   if (read(readyfd[0],&i,sizeof(i))!=sizeof(i))
     fail_message("Server failed to initialize.");
   close(readyfd[0]);

   if (history_bytes>0)
     write_history_mark(0);

   if (*active_session_name) {
      char alias_fn[PATH_MAX];
      char alias_target[64];
      int name_fd;

      name_fd=open(session_name_fn,O_WRONLY|O_CREAT|O_TRUNC,0600);
      if (name_fd<0)
	fail_errno("Could not create session name file");
      write(name_fd,active_session_name,strlen(active_session_name));
      write(name_fd,"\n",1);
      close(name_fd);

      make_alias_filename(active_session_name,alias_fn,sizeof(alias_fn));
      snprintf(alias_target,sizeof(alias_target),"%ld.d",(long)server_pid);
      if (symlink(alias_target,alias_fn)<0) {
	 pid_t existing_pid;

	 if ((errno==EEXIST) &&
	     alias_pid_for_name(active_session_name,&existing_pid)) {
	    if (process_belongs_to_user(existing_pid))
	      fail_messagef("Session name '%s' is already in use.",
			    active_session_name);

	    unlink(alias_fn);
	    if (symlink(alias_target,alias_fn)>=0)
	      goto alias_done;
	 }

	 fail_errno("Could not create session alias");
      }
alias_done:
      ;
   }

   if (verbose)
     fprintf(stderr,"...server PID is %d.\n",server_pid);
   if (no_client)
     printf("%d\n",server_pid);

   started_new_server=1;
   have_server=1;
}

/**********************************************************************/

/* client */

void run_client(void) {
   int i;
   struct pollfd poll_fds[2];
   struct winsize window_size;
   int timeout_ms;
   char up_buf[READ_SIZE],down_buf[READ_SIZE];
   int up_used=0,up_sent=0,down_used=0,down_sent=0;
   struct sigaction sa={0};

   if (verbose)
     fputs("Starting client.\n",stderr);

   client_used=1;
   client_exit_reason=CLIENT_EXIT_UNKNOWN;
   requested_client_detach=0;
   print_client_status(0);
   write_client_pid();

   fcntl(STDIN_FILENO,F_SETFL,O_RDONLY|O_NONBLOCK);
   fcntl(down_fd,F_SETFL,O_WRONLY|O_NONBLOCK);
   fcntl(up_fd,F_SETFL,O_RDONLY|O_NONBLOCK);
   fcntl(STDOUT_FILENO,F_SETFL,O_WRONLY|O_NONBLOCK);

   replay_history_file();
   set_rawmode();

   sa.sa_handler=sigwinch_handler;
   sigaction(SIGWINCH,&sa,NULL);
   sa.sa_handler=sigusr1_handler;
   sigaction(SIGUSR1,&sa,NULL);

   timeout_ms=1;
   while (process_belongs_to_user(server_pid)) {
      if (requested_client_detach) {
	 client_exit_reason=CLIENT_EXIT_DETACHED;
	 break;
      }

      if (waitpid(server_pid,NULL,WNOHANG)==server_pid) {
	client_exit_reason=CLIENT_EXIT_CLOSED;
	break;
      }

      if (check_window) {
	 if (slave_fd<0)
	   slave_fd=open(slavelink_fn,O_RDWR);
	 if (slave_fd>=0) {
	    ioctl(find_tty(),TIOCGWINSZ,&window_size);
	    ioctl(slave_fd,TIOCSWINSZ,&window_size);
	 }
	 check_window=0;
      }
	 
      if (down_used==0) {
	 poll_fds[0].fd=STDIN_FILENO;
	 poll_fds[0].events=POLLIN;
      } else {
	 poll_fds[0].fd=down_fd;
	 poll_fds[0].events=POLLOUT;
      }
      poll_fds[0].revents=0;

      if (up_used==0) {
	 poll_fds[1].fd=up_fd;
	 poll_fds[1].events=POLLIN;
      } else {
	 poll_fds[1].fd=STDOUT_FILENO;
	 poll_fds[1].events=POLLOUT;
      }
      poll_fds[1].revents=0;

      if (poll(poll_fds,2,timeout_ms)==0) {
	 timeout_ms*=3;
	 if (timeout_ms>5000)
	   timeout_ms=5000;
      } else
	timeout_ms=1;

      if (down_used==0) {
	 if (poll_fds[0].revents&(POLLERR|POLLHUP|POLLNVAL)) {
	    client_exit_reason=CLIENT_EXIT_DETACHED;
	    break;
	 }

	 if (poll_fds[0].revents&POLLIN) {
	    i=read(STDIN_FILENO,down_buf,READ_SIZE);
	    if (i>0) {
	      if ((i==1) && (down_buf[0]==0x04)) {
		 client_exit_reason=CLIENT_EXIT_DETACHED;
		 break;
	      }
	      down_used=i;
	    }
	    else if (i==0) {
	      client_exit_reason=CLIENT_EXIT_DETACHED;
	      break;
	    }
	 }

      } else {
	 if (poll_fds[0].revents&POLLOUT) {
	    i=write(down_fd,down_buf+down_sent,down_used-down_sent);
	    if (i>0) down_sent+=i;
	    if (down_sent>=down_used) {
	       down_used=0;
	       down_sent=0;
	    }
	 }
      }

      if (up_used==0) {
	 if (poll_fds[1].revents&POLLIN) {
	    i=read(up_fd,up_buf,READ_SIZE);
	    if (i>0) up_used=i;
	 }

      } else {
	 if (poll_fds[1].revents&(POLLERR|POLLHUP|POLLNVAL)) {
	    client_exit_reason=CLIENT_EXIT_DETACHED;
	    break;
	 }

	 if (poll_fds[1].revents&POLLOUT) {
	    i=write(STDOUT_FILENO,up_buf+up_sent,up_used-up_sent);
	    if (i>0)
	      up_sent+=i;
	    else if ((i<0) && (errno!=EAGAIN) && (errno!=EWOULDBLOCK)) {
	      client_exit_reason=CLIENT_EXIT_DETACHED;
	      break;
	    }
	    if (up_sent>=up_used) {
	       up_used=0;
	       up_sent=0;
	    }
	 }
      }
   }

   if ((client_exit_reason==CLIENT_EXIT_UNKNOWN) &&
       (!process_belongs_to_user(server_pid)))
     client_exit_reason=CLIENT_EXIT_CLOSED;
}

void close_connection(void) {
   int server_alive;

   if (verbose)
     fputs("Closing connection to server.\r\n",stderr);

   server_alive=process_belongs_to_user(server_pid);
   if ((client_exit_reason==CLIENT_EXIT_UNKNOWN) && server_alive)
     client_exit_reason=CLIENT_EXIT_DETACHED;
   else if ((client_exit_reason==CLIENT_EXIT_UNKNOWN) && (!server_alive))
     client_exit_reason=CLIENT_EXIT_CLOSED;

   if (slave_fd>=0)
     close(slave_fd);
   if (up_fd>=0)
     close(up_fd);
   if (down_fd>=0)
     close(down_fd);
   clear_client_pid_if_current();

   if (!no_client)
     print_client_status(1);

   if ((!no_client) && (!server_alive))
     waitpid(server_pid,NULL,0);
}


/**********************************************************************/

/* main program */

int main(int argc,char **argv) {
   handle_command_line(argc,argv);
   validate_requested_session_name();

   if (management_mode==MGMT_DETACH) {
      detach_target_session();
      return 0;
   } else if (management_mode==MGMT_RENAME) {
      rename_target_session();
      return 0;
   }

   if (!allow_nesting)
     prevent_nesting();

   if (!no_search)
     search_for_server();
   if ((!no_server) && (!have_server))
     start_server();

   if (have_server) {
      if (!no_client)
	run_client();
      close_connection();

   } else if (!no_client) {
      if (*requested_session_name && no_server)
	fprintf(stderr,"No session named '%s'.\n",requested_session_name);
      else
	fputs("No server.\n",stderr);
      exit(1);
   }

   return 0;
}
