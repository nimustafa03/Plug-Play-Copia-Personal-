#ifndef GESTOR_KM_H
#define GESTOR_KM_H

#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/dictionary.h>

extern t_log*         logger;
extern t_config*      config;
extern t_dictionary*  diccionario_procesos;
extern int            fd_swap;
extern int            fd_memory_stick;
extern int            fd_kernel_scheduler;

#endif // GESTOR_KM_H