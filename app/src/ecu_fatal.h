#ifndef DRG27_ECU_FATAL_H_
#define DRG27_ECU_FATAL_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * True once the safe-output primitive has been re-asserted at PRE_KERNEL_2,
 * confirming the board's PRE_KERNEL_1 registration was reached.
 */
bool ecu_safe_outputs_confirmed(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_ECU_FATAL_H_ */
