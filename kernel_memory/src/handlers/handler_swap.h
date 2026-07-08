#ifndef HANDLER_SWAP_H_
#define HANDLER_SWAP_H_

#include <stdint.h>
#include <stdbool.h>

void atender_swap(int fd_swap);

// CP3: usadas por la lógica de suspensión/des-suspensión (mover segmentos a bloques).
bool swap_escribir_bloque(int nro_bloque, void* datos);   // datos = 1 bloque
bool swap_leer_bloque(int nro_bloque, void* buffer_out);  // buffer_out = 1 bloque
int  swap_get_block_size(void);
int  swap_get_total_size(void);

#endif