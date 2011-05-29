/* $Id$ */
/*
** Copyright (C) 2002-2011 Sourcefire, Inc.
** Copyright (C) 1998-2002 Martin Roesch <roesch@sourcefire.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/


#ifndef DEBUG_H
#define DEBUG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if !defined(INLINE)
#define INLINE inline
#endif /* !def INLINE */

#include <ctype.h>

#define LOG_VARIABLE "LOCO_DEBUG"

#define LOG_ALL          0xffffffff
#define LOG_FATAL        0x00000001
#define LOG_ERROR        0x00000002
#define LOG_WARN         0x00000004
#define LOG_INFO         0x00000008
#define LOG_DEBUG        0x00000010

void LogMessage(int level, char *fmt, ...);

extern char *DebugMessageFile;
extern int DebugMessageLine;

#define ulog   DebugMessageFile = __FILE__; DebugMessageLine = __LINE__; LogMessage

int GetLogLevel (void);


#endif /* DEBUG_H */
