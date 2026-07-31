#ifndef HANDLER_KERNEL_SCHEDULER_H_
#define HANDLER_KERNEL_SCHEDULER_H_

void atender_kernel_scheduler(int fd_kernel_scheduler);

void atender_creacion_proceso();

char*completar_path(char*path_incompleto);

void notificar_memoria_corrupta_a_ks(void);
#endif