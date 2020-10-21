#ifdef USE_FREEMEM
#include "string.h"
#include "common.h"
#include "vm.h"
#include "freemem.h"
#include "paging.h"

/* This file implements a simple page allocator (SPA)
 * which stores the pages based on a linked list.
 * Each of the element of linked list is stored in the header of each free page.

 * SPA does not use any additional data structure (e.g., linked list).
 * Instead, it uses the pages themselves to store the linked list pointer.
 * Thus, each of the free pages contains the pointer to the next free page
 * which can be dereferenced by NEXT_PAGE() macro.
 * spa_free_pages will only hold the head and the tail pages so that
 * SPA can allocate/free a page in constant time. */

static struct pg_list spa_free_pages;
static struct pg_list spa_free_pages_nvm;


uintptr_t initbase;
size_t initsize;

/* get a free page from the simple page allocator */
uintptr_t
__spa_get(bool zero)
{
  uintptr_t free_page;

  if (LIST_EMPTY(spa_free_pages)) {
    /* try evict a page */
#ifdef USE_PAGING
    uintptr_t new_pa = paging_evict_and_free_one(0);
    if(new_pa)
    {
      spa_put(__va(new_pa));
    }
    else
#endif
    {
      warn("eyrie simple page allocator cannot evict and free pages");
      return 0;
    }
  }

  free_page = spa_free_pages.head;
  assert(free_page);

  /* update list head */
  uintptr_t next = NEXT_PAGE(spa_free_pages.head);
  spa_free_pages.head = next;
  spa_free_pages.count--;

  assert(free_page > EYRIE_LOAD_START && free_page < (freemem_va_start + freemem_size));

  if (zero)
    memset((void*)free_page, 0, RISCV_PAGE_SIZE);

  return free_page;
}


/* get a free page from the simple page allocator */
uintptr_t
__spa_get_nvm(bool zero)
{
  uintptr_t free_page;

  if (LIST_EMPTY(spa_free_pages_nvm)) {
    /* try evict a page */
#ifdef USE_PAGING
    uintptr_t new_pa = paging_evict_and_free_one(0);
    if(new_pa)
    {
      spa_put_nvm(__va(new_pa));
    }
    else
#endif
    {
      warn("eyrie simple page allocator cannot evict and free pages");
      return 0;
    }
  }

  free_page = spa_free_pages_nvm.head;
  assert(free_page);

  /* update list head */
  uintptr_t next = NEXT_PAGE(spa_free_pages_nvm.head);
  spa_free_pages_nvm.head = next;
  spa_free_pages_nvm.count--;

  //assert(free_page > EYRIE_LOAD_START && free_page < (freemem_va_start + freemem_size));

  if (zero)
    memset((void*)free_page, 0, RISCV_PAGE_SIZE);

  return free_page;
}


uintptr_t spa_get() { return __spa_get(false); }
uintptr_t spa_get_zero() { return __spa_get(true); }

uintptr_t spa_get_nvm() { return __spa_get_nvm(false); }
uintptr_t spa_get_zero_nvm() { return __spa_get_nvm(true); }

/* put a page to the simple page allocator */
void
spa_put(uintptr_t page_addr)
{
  uintptr_t prev;

  assert(IS_ALIGNED(page_addr, RISCV_PAGE_BITS));
  printf("[MY_RUNTIME] spa_put() page_addr: 0x%x, EYRIE_LOAD_START: 0x%x, freemem_va_start: 0x%x, freemem_size: 0x%x \n", page_addr, EYRIE_LOAD_START, freemem_va_start, freemem_size);
  assert(page_addr >= EYRIE_LOAD_START && page_addr < (freemem_va_start  + freemem_size));

  if (!LIST_EMPTY(spa_free_pages)) {
    printf("!LIST_EMPTY(spa_free_pages)\n");
    prev = spa_free_pages.tail;
    assert(prev);
    NEXT_PAGE(prev) = page_addr;
  } else {
    printf("else\n");
    spa_free_pages.head = page_addr;
  }

  NEXT_PAGE(page_addr) = 0;
  spa_free_pages.tail = page_addr;

  spa_free_pages.count++;
  printf("dram free pages after increment: %d\n", spa_free_pages.count);
  return;
}


void
spa_put_nvm(uintptr_t page_addr)
{
  uintptr_t prev;

  assert(IS_ALIGNED(page_addr, RISCV_PAGE_BITS));
  printf("[MY_RUNTIME] spa_put_nvm() page_addr: 0x%x \n", page_addr);
  //assert(page_addr >= EYRIE_LOAD_START && page_addr < (freemem_va_start  + freemem_size));

  if (!LIST_EMPTY(spa_free_pages_nvm)) {
    printf("!LIST_EMPTY(spa_free_pages_nvm)\n");
    prev = spa_free_pages_nvm.tail;
    assert(prev);
    NEXT_PAGE(prev) = page_addr;
  } else {
    printf("else\n");
    spa_free_pages_nvm.head = page_addr;
  }

  printf("Going to next page\n");

  NEXT_PAGE(page_addr) = 0;
  spa_free_pages_nvm.tail = page_addr;

  spa_free_pages_nvm.count++;
  printf("nvm free pages after increment: %d\n", spa_free_pages_nvm.count);
  return;
}


unsigned int
spa_available(){
#ifndef USE_PAGING
  return spa_free_pages.count;
#else
  return spa_free_pages.count + paging_remaining_pages();
#endif
}


unsigned int
spa_available_nvm(){
#ifndef USE_PAGING
  return spa_free_pages_nvm.count;
#else
  return spa_free_pages_nvm.count + paging_remaining_pages();
#endif
}


void
spa_init(uintptr_t base, size_t size)
{
  uintptr_t cur;

  LIST_INIT(spa_free_pages);

  // both base and size must be page-aligned
  assert(IS_ALIGNED(base, RISCV_PAGE_BITS));
  assert(IS_ALIGNED(size, RISCV_PAGE_BITS));

  initbase = base;
  initsize = size;

  /* put all free pages in freemem (base) into spa_free_pages */
  for(cur = base;
      cur < base + size;
      cur += RISCV_PAGE_SIZE) {
    spa_put(cur);
  }
  printf("[MY_RUNTIME] spa_init() Free pages: %d \n", spa_free_pages.count);
}


void
spa_add_dram(uintptr_t base, size_t size)
{
  uintptr_t cur;



  // both base and size must be page-aligned
  assert(IS_ALIGNED(base, RISCV_PAGE_BITS));
  assert(IS_ALIGNED(size, RISCV_PAGE_BITS));

  initbase = base;
  initsize = size;

  /* put all free pages in freemem (base) into spa_free_pages */
  for(cur = base;
      cur < base + size;
      cur += RISCV_PAGE_SIZE) {
    spa_put(cur);
  }
  printf("[MY_RUNTIME] spa_add_dram() Free pages: %d \n", spa_free_pages.count);
}


//===LENA ADD===///
//===Add the free pages based on the extended memory===///
void spa_init_nvm(uintptr_t base, size_t size){
    uintptr_t cur;
    LIST_INIT(spa_free_pages_nvm);
    printf("Initialized list\n");
    // both base and size must be page-aligned
  assert(IS_ALIGNED(base, RISCV_PAGE_BITS));
  assert(IS_ALIGNED(size, RISCV_PAGE_BITS));

  //===Start to add pages from where it ended in spa_init===///
  for(cur = base;
      cur < (base + size);
      cur += RISCV_PAGE_SIZE) {
    spa_put_nvm(cur);
  }
  printf("[MY_RUNTIME] spa_init_nvm() Free pages: %d \n", spa_free_pages_nvm.count);
    
}
#endif // USE_FREEMEM




