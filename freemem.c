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

uintptr_t spa_get() { return __spa_get(false); }
uintptr_t spa_get_zero() { return __spa_get(true); }

/* put a page to the simple page allocator */
void
spa_put(uintptr_t page_addr)
{
  uintptr_t prev;

  assert(IS_ALIGNED(page_addr, RISCV_PAGE_BITS));
  printf("[MY_RUNTIME] page_addr: 0x%x, EYRIE_LOAD_START: 0x%x, freemem_va_start: 0x%x, freemem_size: 0x%x \n", page_addr, EYRIE_LOAD_START, freemem_va_start, freemem_size);
  assert(page_addr >= EYRIE_LOAD_START && page_addr < (freemem_va_start  + freemem_size));

  if (!LIST_EMPTY(spa_free_pages)) {
    prev = spa_free_pages.tail;
    assert(prev);
    NEXT_PAGE(prev) = page_addr;
  } else {
    spa_free_pages.head = page_addr;
  }

  NEXT_PAGE(page_addr) = 0;
  spa_free_pages.tail = page_addr;

  spa_free_pages.count++;
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

//===LENA ADD===///
//===Add the free pages based on the extended memory===///
void my_spa_add(uintptr_t base, size_t size){
    uintptr_t cur;
    // both base and size must be page-aligned
  assert(IS_ALIGNED(base, RISCV_PAGE_BITS));
  assert(IS_ALIGNED(size, RISCV_PAGE_BITS));

  //===Start to add pages from where it ended in spa_init===///
  for(cur = initbase + initsize;
      cur < (initbase + initsize) + size;
      cur += RISCV_PAGE_SIZE) {
    spa_put(cur);
  }
  printf("[MY_RUNTIME] my_spa_add() Free pages: %d \n", spa_free_pages.count);
    
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
#endif // USE_FREEMEM


