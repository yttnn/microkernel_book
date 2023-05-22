#pragma once
#include <libs/common/hinavm_types.h>

#define HINAVM_INSTS_MAX 128

struct hinavm {
  hinavm_inst_t insts[HINAVM_INSTS_MAX];
  uint32_t num_insts;
};

__noretrun void hinavm_run(struct hinavm *hinavm);