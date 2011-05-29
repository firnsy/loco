#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"

#define STD_BUF 1024

#ifdef DEBUG
int loglevel = LOG_ALL;
char *DebugMessageFile = NULL;
int DebugMessageLine = 0;

int GetLogLevel(void)
{
  static int log_init = 0;
  static unsigned int log_level = 0;

  // declared here for compatibility with older compilers
  // not initialized here cuz the next step is done once
  const char* key;

  if (log_init)
    return log_level;

  key = getenv(LOG_VARIABLE);

  if (key)
    log_level = strtoul(key, NULL, 0);
  else
    log_level = 0;

  log_init = 1;
  return log_level;
}

void LogMessage(int level, char *fmt, ...)
{
  va_list ap;
  int log_level = GetLogLevel();

  if ( !(level & log_level) )
    return;

  va_start(ap, fmt);
        
  if (0) //(snort_conf != NULL) && ScDaemonMode())
  {
    char buf[STD_BUF];
    int buf_len = sizeof(buf);
    char *buf_ptr = buf;

    buf[buf_len - 1] = '\0';

    /* filename and line number information */
    if ((log_level > LOG_DEBUG) &&
        (DebugMessageFile != NULL))
    {
      snprintf(buf, buf_len - 1, "%s:%d: ",
               DebugMessageFile, DebugMessageLine);
      buf_ptr += strlen(buf);
      buf_len -= strlen(buf);
    }

    vsnprintf(buf_ptr, buf_len - 1, fmt, ap);
//        syslog(LOG_DAEMON | LOG_DEBUG, "%s", buf);
  }
  else
  {
    if ((log_level > LOG_DEBUG) &&
        (DebugMessageFile != NULL))
      printf("%s:%d: ", DebugMessageFile, DebugMessageLine);

      vprintf(fmt, ap);
  }

  va_end(ap);
}
#else
void LogMessage(int level, char *fmt, ...)
{
}
#endif /* DEBUG */
