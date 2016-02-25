/*
Copyright (c) 2007-2013. The YARA Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#if defined(_WIN32) || defined(__CYGWIN__)

#include <windows.h>

#include <yara/mem.h>
#include <yara/error.h>
#include <yara/proc.h>


int _yr_attach_process(
    int pid,
    void** hProcess)
{
  TOKEN_PRIVILEGES tokenPriv;
  LUID luidDebug;
  HANDLE hToken = NULL;

  if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken) &&
    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidDebug))
  {
    tokenPriv.PrivilegeCount = 1;
    tokenPriv.Privileges[0].Luid = luidDebug;
    tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tokenPriv,
        sizeof(tokenPriv),
        NULL,
        NULL);
  }

  if (hToken != NULL)
    CloseHandle(hToken);

  *hProcess = OpenProcess(
      PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
      FALSE,
      pid);

  if (*hProcess == NULL)
    return ERROR_COULD_NOT_ATTACH_TO_PROCESS;

  return ERROR_SUCCESS;
}

int _yr_detach_process(
    void* hProcess)
{
  if (hProcess != NULL)
    CloseHandle(hProcess);

  return ERROR_SUCCESS;
}

int _yr_get_process_blocks(
    void* hProcess,
    YR_MEMORY_BLOCK** head)
{
  PVOID address;
  int result = ERROR_SUCCESS;
  int sections = 0;

  YR_MEMORY_BLOCK* new_block;
  YR_MEMORY_BLOCK* current = NULL;

  SYSTEM_INFO si;
  MEMORY_BASIC_INFORMATION mbi;

  GetSystemInfo(&si);

  address = si.lpMinimumApplicationAddress;

  while (address < si.lpMaximumApplicationAddress &&
    VirtualQueryEx(hProcess, address, &mbi, sizeof(mbi)) != 0)
  {
    if (mbi.State == MEM_COMMIT && ((mbi.Protect & PAGE_NOACCESS) == 0)) // TODO: check for read permission?
    {
      new_block = (YR_MEMORY_BLOCK*)yr_malloc(sizeof(YR_MEMORY_BLOCK));

      new_block->base = (size_t)mbi.BaseAddress;
      new_block->size = mbi.RegionSize;

      if (*head == NULL)
        *head = new_block;

      if (current != NULL)
        current->next = new_block;

      current = new_block;

      ++sections;
    }

    address = (uint8_t*)address + mbi.RegionSize;
  }

  printf("%lu sections\n", sections);

  return result;
}

int _yr_read_process_block(
    void* hProcess,
    YR_MEMORY_BLOCK* block,
    uint8_t** data)
{
  SIZE_T read;
  uint8_t* buffer = NULL;
  int result = ERROR_SUCCESS;
  *data = NULL;

  buffer = (uint8_t*)yr_malloc(block->size);

  if (buffer == NULL)
    return ERROR_INSUFICIENT_MEMORY;

  if (ReadProcessMemory(
      (HANDLE)hProcess,
      (LPCVOID)block->base,
      buffer,
      (SIZE_T)block->size,
      &read) == FALSE)
  {
    result = ERROR_COULD_NOT_READ_PROCESS_MEMORY;

    if (buffer != NULL)
    {
      yr_free(buffer);
      buffer = NULL;
    }
  }

  // TODO: compare read with block size

  *data = buffer;

  return result;
}



//int yr_process_get_memory(
//    int pid,
//    YR_MEMORY_BLOCK** first_block)
//{
//  PVOID address;
//  SIZE_T read;
//
//  unsigned char* data;
//  int result = ERROR_SUCCESS;
//
//  SYSTEM_INFO si;
//  MEMORY_BASIC_INFORMATION mbi;
//
//  YR_MEMORY_BLOCK* new_block;
//  YR_MEMORY_BLOCK* current_block = NULL;
//
//  TOKEN_PRIVILEGES tokenPriv;
//  LUID luidDebug;
//  HANDLE hProcess = NULL;
//  HANDLE hToken = NULL;
//
//  if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken) &&
//      LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidDebug))
//  {
//    tokenPriv.PrivilegeCount = 1;
//    tokenPriv.Privileges[0].Luid = luidDebug;
//    tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
//
//    AdjustTokenPrivileges(
//        hToken,
//        FALSE,
//        &tokenPriv,
//        sizeof(tokenPriv),
//        NULL,
//        NULL);
//  }
//
//  hProcess = OpenProcess(
//      PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
//      FALSE,
//      pid);
//
//  *first_block = NULL;
//
//  if (hProcess == NULL)
//  {
//    if (hToken != NULL)
//      CloseHandle(hToken);
//
//    return ERROR_COULD_NOT_ATTACH_TO_PROCESS;
//  }
//
//  GetSystemInfo(&si);
//
//  address = si.lpMinimumApplicationAddress;
//  size_t allocated = 0;
//
//  while (address < si.lpMaximumApplicationAddress &&
//         VirtualQueryEx(hProcess, address, &mbi, sizeof(mbi)) != 0)
//  {
//    if (mbi.State == MEM_COMMIT && ((mbi.Protect & PAGE_NOACCESS) == 0))
//    {
//      data = (unsigned char*) yr_malloc(mbi.RegionSize);
//
//      if (data == NULL)
//      {
//        result = ERROR_INSUFICIENT_MEMORY;
//        break;
//      }
//
//      allocated += mbi.RegionSize;
//
//      if (ReadProcessMemory(
//              hProcess,
//              mbi.BaseAddress,
//              data,
//              mbi.RegionSize,
//              &read))
//      {
//        new_block = (YR_MEMORY_BLOCK*) yr_malloc(sizeof(YR_MEMORY_BLOCK));
//
//        if (new_block == NULL)
//        {
//          yr_free(data);
//          result = ERROR_INSUFICIENT_MEMORY;
//          break;
//        }
//
//        if (*first_block == NULL)
//          *first_block = new_block;
//
//        new_block->base = (size_t) mbi.BaseAddress;
//        new_block->size = mbi.RegionSize;
//        new_block->data = data;
//        new_block->next = NULL;
//
//        if (current_block != NULL)
//          current_block->next = new_block;
//
//        current_block = new_block;
//      }
//      else
//      {
//        yr_free(data);
//      }
//    }
//
//    address = (PVOID)((ULONG_PTR) mbi.BaseAddress + mbi.RegionSize);
//  }
//
//  printf("Allocated %lu bytes\n", allocated);
//
//  if (hToken != NULL)
//    CloseHandle(hToken);
//
//  if (hProcess != NULL)
//    CloseHandle(hProcess);
//
//  return result;
//}

#else

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
    defined(__OpenBSD__) || defined(__MACH__)
#else
#define _XOPEN_SOURCE 500
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <yara/error.h>
#include <yara/proc.h>
#include <yara/mem.h>

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
    defined(__OpenBSD__) || defined(__MACH__)
#define PTRACE_ATTACH PT_ATTACH
#define PTRACE_DETACH PT_DETACH
#endif

#if defined(__MACH__)

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>

int yr_process_get_memory(
    pid_t pid,
    YR_MEMORY_BLOCK** first_block)
{
  task_t task;
  kern_return_t kr;

  vm_size_t size = 0;
  vm_address_t address = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count;
  mach_port_t object;

  unsigned char* data;

  YR_MEMORY_BLOCK* new_block;
  YR_MEMORY_BLOCK* current_block = NULL;

  *first_block = NULL;

  if ((kr = task_for_pid(mach_task_self(), pid, &task)) != KERN_SUCCESS)
    return ERROR_COULD_NOT_ATTACH_TO_PROCESS;

  do {

    info_count = VM_REGION_BASIC_INFO_COUNT_64;

    kr = vm_region_64(
        task,
        &address,
        &size,
        VM_REGION_BASIC_INFO,
        (vm_region_info_t) &info,
        &info_count,
        &object);

    if (kr == KERN_SUCCESS)
    {
      data = (unsigned char*) yr_malloc(size);

      if (data == NULL)
        return ERROR_INSUFICIENT_MEMORY;

      if (vm_read_overwrite(
              task,
              address,
              size,
              (vm_address_t)
              data,
              &size) == KERN_SUCCESS)
      {
        new_block = (YR_MEMORY_BLOCK*) yr_malloc(sizeof(YR_MEMORY_BLOCK));

        if (new_block == NULL)
        {
          yr_free(data);
          return ERROR_INSUFICIENT_MEMORY;
        }

        if (*first_block == NULL)
          *first_block = new_block;

        new_block->base = address;
        new_block->size = size;
        new_block->data = data;
        new_block->next = NULL;

        if (current_block != NULL)
          current_block->next = new_block;

        current_block = new_block;
      }
      else
      {
        yr_free(data);
      }

      address += size;
    }


  } while (kr != KERN_INVALID_ADDRESS);

  if (task != MACH_PORT_NULL)
    mach_port_deallocate(mach_task_self(), task);

  return ERROR_SUCCESS;
}

#else

#include <errno.h>

int yr_process_get_memory(
    pid_t pid,
    YR_MEMORY_BLOCK** first_block)
{
  char buffer[256];
  unsigned char* data = NULL;
  size_t begin, end, length;

  YR_MEMORY_BLOCK* new_block;
  YR_MEMORY_BLOCK* current_block = NULL;

  FILE *maps = NULL;

  int mem = -1;
  int result;
  int attached = 0;

  *first_block = NULL;

  snprintf(buffer, sizeof(buffer), "/proc/%u/maps", pid);

  maps = fopen(buffer, "r");

  if (maps == NULL)
  {
    result = ERROR_COULD_NOT_ATTACH_TO_PROCESS;
    goto _exit;
  }

  snprintf(buffer, sizeof(buffer), "/proc/%u/mem", pid);

  mem = open(buffer, O_RDONLY);

  if (mem == -1)
  {
    result = ERROR_COULD_NOT_ATTACH_TO_PROCESS;
    goto _exit;
  }

  if (ptrace(PTRACE_ATTACH, pid, NULL, 0) != -1)
  {
    attached = 1;
  }
  else
  {
    result = ERROR_COULD_NOT_ATTACH_TO_PROCESS;
    goto _exit;
  }

  wait(NULL);

  while (fgets(buffer, sizeof(buffer), maps) != NULL)
  {
    sscanf(buffer, "%zx-%zx", &begin, &end);

    length = end - begin;

    data = yr_malloc(length);

    if (data == NULL)
    {
      result = ERROR_INSUFICIENT_MEMORY;
      goto _exit;
    }

    if (pread(mem, data, length, begin) != -1)
    {
      new_block = (YR_MEMORY_BLOCK*) yr_malloc(sizeof(YR_MEMORY_BLOCK));

      if (new_block == NULL)
      {
        result = ERROR_INSUFICIENT_MEMORY;
        goto _exit;
      }

      if (*first_block == NULL)
        *first_block = new_block;

      new_block->base = begin;
      new_block->size = length;
      new_block->data = data;
      new_block->next = NULL;

      if (current_block != NULL)
        current_block->next = new_block;

      current_block = new_block;
    }
    else
    {
      yr_free(data);
      data = NULL;
    }
  }

  result = ERROR_SUCCESS;

_exit:

  if (attached)
    ptrace(PTRACE_DETACH, pid, NULL, 0);

  if (mem != -1)
    close(mem);

  if (maps != NULL)
    fclose(maps);

  if (data != NULL)
    yr_free(data);

  return result;
}

#endif
#endif

// process iterator abstraction

static int _yr_free_block_data(
    YR_PROCESS_CONTEXT* context)
{
  if (context->data != NULL)
  {
    yr_free(context->data);
    context->data = NULL;
  }

  return ERROR_SUCCESS;
}

static YR_MEMORY_BLOCK* _yr_get_first_block(
    YR_BLOCK_ITERATOR* iterator)
{
  printf("!!! first block\n");

  YR_PROCESS_CONTEXT* ctx = (YR_PROCESS_CONTEXT*)iterator->context;

  ctx->current = ctx->blocks;

  _yr_free_block_data(ctx);

  return ctx->current;
}

static YR_MEMORY_BLOCK* _yr_get_next_block(
    YR_BLOCK_ITERATOR* iterator)
{
  printf("next block\n");

  YR_PROCESS_CONTEXT* ctx = (YR_PROCESS_CONTEXT*)iterator->context;

  if (ctx->current == NULL)
    return NULL;

  ctx->current = ctx->current->next;

  _yr_free_block_data(ctx);

  return ctx->current;
}

static uint8_t* _yr_fetch_block_data(
    YR_BLOCK_ITERATOR* iterator)
{
  printf("fetching block\n");

  YR_PROCESS_CONTEXT* ctx = (YR_PROCESS_CONTEXT*)iterator->context;

  if (ctx->current == NULL)
    return NULL;

  _yr_free_block_data(ctx);

  _yr_read_process_block(
      ctx->process_context,
      ctx->current,
      &ctx->data);

  return ctx->data;
}

int yr_open_process_iterator(
    int pid,
    YR_BLOCK_ITERATOR* iterator)
{
  YR_PROCESS_CONTEXT* context = (YR_PROCESS_CONTEXT*)yr_malloc(sizeof(YR_PROCESS_CONTEXT));

  if (context == NULL)
    return ERROR_INSUFICIENT_MEMORY;

  context->blocks = NULL;
  context->current = NULL;
  context->data = NULL;
  context->process_context = NULL;

  iterator->context = context;
  iterator->first = _yr_get_first_block;
  iterator->next = _yr_get_next_block;
  iterator->fetch_data = _yr_fetch_block_data;

  int result = _yr_attach_process(
      pid,
      &context->process_context);

  result = _yr_get_process_blocks(
      context->process_context,
      &context->blocks);

  return result;
}

int yr_close_process_iterator(
    YR_BLOCK_ITERATOR* iterator)
{
  YR_PROCESS_CONTEXT* ctx = (YR_PROCESS_CONTEXT*)iterator->context;

  if (ctx == NULL)
    return ERROR_SUCCESS;

  // NOTE: detach is responsible for freeing allocated process context
  _yr_detach_process(ctx->process_context);

  _yr_free_block_data(ctx);

  YR_MEMORY_BLOCK* current = ctx->blocks;
  YR_MEMORY_BLOCK* next;

  // free blocks list
  while(current != NULL)
  {
    next = current->next;
    yr_free(current);
    current = next;
  }

  // free the context
  yr_free(iterator->context);
  iterator->context = NULL;

  return ERROR_SUCCESS;
}
