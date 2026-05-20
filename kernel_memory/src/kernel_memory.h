#ifndef KERNELMEMORY_H
#ifndef KERNELMEMORY_H
#include <stdint.h>

typedef struct 
{
    uint32_t pid;
    t_registros registros;

    char** instrucciones;
    int cantidad_instrucciones;

    t_list* tabla_segmentos; // CP3
} t_contexto_ejecucion;

typedef struct
{
    uint8_t AX;
    uint8_t BX;
    uint8_t CX;
    uint8_t DX;
    
    uint32_t EAX;
    uint32_t EBX;
    uint32_t ECX;
    uint32_t EDX;
    
    uint32_t SI;
    uint32_t DI;
    
    uint32_t PC;
} t_registros; /* NICO M: ¿Convendría tener esta estructura en otro lado? Siento que la CPU la podría necesitar. Idem con el contexto de ejecución.
## SI ALGUIEN DECIDE MOVERLO. PLS CAMBIAR EL INCLUDE DE KERNEL MEMORY O AVISARME PARA QUE LO HAGA YO.
*/

char** cargar_instrucciones(uint32_t pid);