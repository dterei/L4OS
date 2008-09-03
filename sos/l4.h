/****************************************************************************
 *
 *      $Id: $
 *
 *      Description: Wrapper to include all of the l4 headers.  We need this
 *                   here because the documentation doesn't include which file
 *                   needs to be included for which api.
 *
 *      Author:		    Godfrey van der Linden
 *
 ****************************************************************************/

#ifndef _L4_H
#define _L4_H

#include <bootinfo/bootinfo.h>
#include <l4/cache.h>
#include <l4/caps.h>
#include <l4/config.h>
#include <l4/ipc.h>
#include <l4/kdebug.h>
#include <l4/map.h>
#include <l4/message.h>
#include <l4/misc.h>
#include <l4/schedule.h>
#include <l4/space.h>
#include <l4/thread.h>
#include <l4/time.h>
#include <l4/types.h>

#endif // _L4_H
