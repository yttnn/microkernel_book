#include "vm.h"
#include "asm.h"
#include "mp.h"
#include "plic.h"
#include "uart.h"
#include <kernel/arch.h>
#include <kernel/memory.h>
#include <kernel/printk.h>
#include <libs/common/string.h>

static struct arch_vm kernel_vm;

// PAGE_* マクロで指定したページ属性をSv32のそれに変換する。
static pte_t page_attrs_to_pte_flags(unsigned attrs) {
    return ((attrs & PAGE_READABLE) ? PTE_R : 0)
           | ((attrs & PAGE_WRITABLE) ? PTE_W : 0)
           | ((attrs & PAGE_EXECUTABLE) ? PTE_X : 0)
           | ((attrs & PAGE_USER) ? PTE_U : 0);
}