/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "thread_attrs_api.h"

#include <windows.h>

#include "sql/log.h"
#include "my_dbug.h"
#include "my_sys.h"

namespace resourcegroups
{
namespace platform
{
bool is_platform_supported()
{
  return true;
}

bool bind_to_cpu(cpu_id_t cpu_id)
{
  return bind_to_cpu(cpu_id, GetCurrentThreadId());
}

bool bind_to_cpu(cpu_id_t cpu_id, my_thread_os_id_t thread_id)
{
  HANDLE handle;
  BOOL res= FALSE;

  handle= OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION,
                     TRUE, thread_id);
  if (handle != nullptr)
  {
    DWORD_PTR cpu_mask= 0;
    cpu_mask|= 1 << cpu_id;
    res= SetThreadAffinityMask(handle, cpu_mask);
    if (!res)
    {
      char errbuf[MYSQL_ERRMSG_SIZE];
      sql_print_error("Unable to bind thread id %llu to cpu id %u."
                      "(error code %d - %-.192s)", thread_id, cpu_id,
                      my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
    }
    CloseHandle(handle);
  }
  else
  {
    char errbuf[MYSQL_ERRMSG_SIZE];
    sql_print_error("bind_to_cpu failed: Failed to get handle for %llu."
                    "(error code %d - %-.192s)", thread_id,
                    my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
  }
  return res == FALSE;
}

bool bind_to_cpus(const std::vector<cpu_id_t> &cpu_ids)
{
  return bind_to_cpus(cpu_ids, GetCurrentThreadId());
}

bool bind_to_cpus(const std::vector<cpu_id_t> &cpu_ids,
                  my_thread_os_id_t thread_id)
{
  if (cpu_ids.empty())
    return false;

  HANDLE handle;
  DWORD_PTR res= 1;

  handle= OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION,
                     TRUE, thread_id);
  if (handle != nullptr)
  {
    DWORD_PTR cpu_mask= 0;
    for(const auto &cpu_id : cpu_ids)
          cpu_mask|= 1 << cpu_id;
    res= SetThreadAffinityMask(handle, cpu_mask);
    if (res == 0)
    {
      char errbuf[MYSQL_ERRMSG_SIZE];
      sql_print_error("Unable to bind thread id %llu to cpu ids."
                      "(error code %d - %-.192s)", thread_id,
                      my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
    }

    CloseHandle(handle);
  }
  else
  {
    char errbuf[MYSQL_ERRMSG_SIZE];
    sql_print_warning("bind_to_cpu failed: Failed to get handle for thread %llu."
                      "(error code %d - %-.192s)", thread_id, my_errno(),
                      my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
  }
  return res == 0;
}

bool unbind_thread()
{
  return unbind_thread(GetCurrentThreadId());
}

bool unbind_thread(my_thread_os_id_t thread_id)
{
  HANDLE handle;
  DWORD_PTR res= 1;

  handle= OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION,
                     TRUE, thread_id);
  if (handle != nullptr)
  {
    DWORD_PTR cpu_mask= sizeof(DWORD_PTR) - 1;
    res= SetThreadAffinityMask(handle, cpu_mask);
    if (res == 0)
    {
      char errbuf[MYSQL_ERRMSG_SIZE];
      sql_print_error("Unable to unbind thread %llu (error code %d - %-.192s)",
                      thread_id, my_errno(),
                      my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
    }
    CloseHandle(handle);
  }
  else
  {
    char errbuf[MYSQL_ERRMSG_SIZE];
    sql_print_error("unbind_thread failed: Failed to get handle for thread "
                    " %llu (error code %d - %-.192s)", thread_id, my_errno(),
                    my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
  }
  return res == 0;
}

int thread_priority(my_thread_os_id_t thread_id)
{
  DBUG_ASSERT(0);
  return 0;
}

static inline int  map_to_win_priority(int priority)
{
  int win_priority;

  if (priority <= -10)
    win_priority= THREAD_PRIORITY_HIGHEST;
  else if (priority > -10 && priority < 0)
    win_priority= THREAD_PRIORITY_ABOVE_NORMAL;
  else if (priority == 0)
    win_priority= THREAD_PRIORITY_NORMAL;
  else if (priority > 0 && priority < 10)
    win_priority= THREAD_PRIORITY_BELOW_NORMAL;
  else
    win_priority= THREAD_PRIORITY_LOWEST;

  return win_priority;
}


bool set_thread_priority(int priority)
{
  DBUG_ASSERT(is_valid_thread_priority(priority));

  BOOL res= SetThreadPriority(GetCurrentThread(),
                              map_to_win_priority(priority));

  return res == FALSE;
}


bool set_thread_priority(int priority, my_thread_os_id_t thread_id)
{
  DBUG_ENTER("set_thread_priority");

  DBUG_ASSERT(is_valid_thread_priority(priority));
  HANDLE handle;
  BOOL res= FALSE;
  handle= OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION,
                     TRUE, thread_id);
  if (handle != nullptr)
    res= SetThreadPriority(handle, map_to_win_priority(priority));
  else
  {
    char errbuf[MYSQL_ERRMSG_SIZE];
    sql_print_error("Set thread priority failed: unable to acquire handle"
                    " thread id %d (error code %d - %-.192s)", thread_id,
                    my_errno(),
                    my_strerror(errbuf, MYSQL_ERRMSG_SIZE, my_errno()));
  }

  DBUG_RETURN(res == FALSE);
}


uint32_t num_vcpus()
{
  cpu_id_t num_vcpus= 0;

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  num_vcpus= si.dwNumberOfProcessors;
  return num_vcpus;
}

bool can_thread_priority_be_set()
{
  return true;
}
}  // platform
}  // resourcegroups
