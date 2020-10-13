#include <asm/csr.h>
#include <stdio.h>
#include "printf.h"
#include "interrupt.h"
#include "syscall.h"
#include "vm.h"
#include "string.h"
#include "sbi.h"
#include "freemem.h"
#include "mm.h"
#include "env.h"
#include "paging.h"

/* defined in vm.h */
extern uintptr_t shared_buffer;
extern uintptr_t shared_buffer_size;

/* initial memory layout */
uintptr_t utm_base;
size_t utm_size;


uintptr_t dram_baseglobal;
uintptr_t dram_sizeglobal;

uintptr_t freemem_va_end;
/* defined in entry.S */
extern void* encl_trap_handler;

void spa_init_nvm(uintptr_t base, size_t size);

void spa_add_dram(uintptr_t base, size_t size);

#ifdef USE_FREEMEM


/* map entire enclave physical memory so that
 * we can access the old page table and free memory */
/* remap runtime kernel to a new root page table */



void
map_physical_memory(uintptr_t dram_base,
                    uintptr_t dram_size)
{
  printf("[MY_RUNTIME] ***map_physical_memory() with dram_base 0x%x and size 0x%x ***\n", dram_base, dram_size);
  uintptr_t ptr = EYRIE_LOAD_START;
  /* load address should not override kernel address */
  assert(RISCV_GET_PT_INDEX(ptr, 1) != RISCV_GET_PT_INDEX(runtime_va_start, 1));
  map_with_reserved_page_table(dram_base, dram_size,
      ptr, load_l2_page_table, load_l3_page_table);
}


void 
my_map_physical_memory(uintptr_t addr, uintptr_t size){
    freemem_size = freemem_size + size;
	
  if ((size%2)!=0){

    ///*** THIS IS WHERE PROBLEM OCCURS ****
    //map_physical_memory(dram_baseglobal, dram_sizeglobal + size);
      // map_physical_memory(addr, (size - 1));

      map_physical_memory(addr, size -1);

      uintptr_t nvm_va_start = __va(addr);
      //uintptr_t nvm_va_end = nvm_va_start + size;

    	spa_init_nvm(nvm_va_start, size - 1);
  } else {
      map_physical_memory(dram_baseglobal, dram_sizeglobal + size);
      spa_add_dram(freemem_va_end, size);
  }

}

void
remap_kernel_space(uintptr_t runtime_base,
                   uintptr_t runtime_size)
{
  /* eyrie runtime is supposed to be smaller than a megapage */
  assert(runtime_size <= RISCV_GET_LVL_PGSIZE(2));

  map_with_reserved_page_table(runtime_base, runtime_size,
     runtime_va_start, kernel_l2_page_table, kernel_l3_page_table);
}

void
copy_root_page_table()
{
  /* the old table lives in the first page */
  pte* old_root_page_table = (pte*) EYRIE_LOAD_START;
  int i;

  /* copy all valid entries of the old root page table */
  for (i = 0; i < BIT(RISCV_PT_INDEX_BITS); i++) {
    if (old_root_page_table[i] & PTE_V &&
        !(root_page_table[i] & PTE_V)) {
      root_page_table[i] = old_root_page_table[i];
    }
  }
}

/* initialize free memory with a simple page allocator*/
void
init_freemem()
{
  spa_init(freemem_va_start, freemem_size);
}

#endif // USE_FREEMEM

/* initialize user stack */
void
init_user_stack_and_env()
{
  void* user_sp = (void*) EYRIE_USER_STACK_START;

#ifdef USE_FREEMEM
  size_t count;
  uintptr_t stack_end = EYRIE_USER_STACK_END;
  size_t stack_count = EYRIE_USER_STACK_SIZE >> RISCV_PAGE_BITS;


  // allocated stack pages right below the runtime
  count = alloc_pages(vpn(stack_end), stack_count,
      PTE_R | PTE_W | PTE_D | PTE_A | PTE_U);

  assert(count == stack_count);

#endif // USE_FREEMEM

  // setup user stack env/aux
  user_sp = setup_start(user_sp);

  // prepare user sp
  csr_write(sscratch, user_sp);
}

void
eyrie_boot(uintptr_t dummy, // $a0 contains the return value from the SBI
           uintptr_t dram_base,
           uintptr_t dram_size,
           uintptr_t runtime_paddr,
           uintptr_t user_paddr,
           uintptr_t free_paddr,
           uintptr_t utm_vaddr,
           uintptr_t utm_size)
{
	printf("[MY_RUNTIME] Eyrie_boot, dram_base: 0x%x, dram_size: 0x%x, runtime_paddr: 0x%x, user_paddr: 0x%x, free_paddr: 0x%x, utm_vaddr: 0x%x, utm_size: 0x%x \n", dram_base, dram_size, runtime_paddr, free_paddr, utm_vaddr, utm_size);
  

 //***LENA ADD**//
  dram_baseglobal = dram_base;
  dram_sizeglobal = dram_size;

  /* set initial values */
  load_pa_start = dram_base;
  shared_buffer = utm_vaddr;
  shared_buffer_size = utm_size;
  runtime_va_start = (uintptr_t) &rt_base;
  kernel_offset = runtime_va_start - runtime_paddr;

  printf("UTM : 0x%lx-0x%lx (%u KB) \n", utm_vaddr, utm_vaddr+utm_size, utm_size/1024);
  printf("DRAM: 0x%lx-0x%lx (%u KB) \n", dram_base, dram_base + dram_size, dram_size/1024);
#ifdef USE_FREEMEM
  freemem_va_start = __va(free_paddr);
  freemem_size = dram_base + dram_size - free_paddr;

  
  freemem_va_end = freemem_va_start + freemem_size;

  printf("FREE: 0x%lx-0x%lx (%u KB), va 0x%lx \n", free_paddr, dram_base + dram_size, freemem_size/1024, freemem_va_start);

  /* remap kernel VA */
  remap_kernel_space(runtime_paddr, user_paddr - runtime_paddr);
  map_physical_memory(dram_base, dram_size);

  /* switch to the new page table */
  csr_write(satp, satp_new(kernel_va_to_pa(root_page_table)));

  /* copy valid entries from the old page table */
  copy_root_page_table();

  /* initialize free memory */
  init_freemem();

  //TODO: This should be set by walking the userspace vm and finding
  //highest used addr. Instead we start partway through the anon space
  set_program_break(EYRIE_ANON_REGION_START + (1024 * 1024 * 1024));

  #ifdef USE_PAGING
  init_paging(user_paddr, free_paddr);
  #endif /* USE_PAGING */
#endif /* USE_FREEMEM */

  /* initialize user stack */
  init_user_stack_and_env();

  /* set trap vector */
  csr_write(stvec, &encl_trap_handler);

  /* prepare edge & system calls */
  init_edge_internals();

  /* set timer */
  init_timer();

  /* Enable the FPU */
  csr_write(sstatus, csr_read(sstatus) | 0x6000);

  debug("eyrie boot finished. drop to the user land ...");
  /* booting all finished, droping to the user land */
  return;
}


