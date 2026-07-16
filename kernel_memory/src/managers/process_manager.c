#include "../estructuras.h"
#include <stdlib.h>
#include <commons/collections/dictionary.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include "../handlers/handler_cpu.h"
#include "../../utils/src/utils/tipos.h"
#include "memory_manager.h"

#include "process_manager.h"

static t_administrador_procesos administrador;
extern t_log*logger;
extern t_config* config; // CP3: para leer SEGMENT_MAX_SIZE en la traducción

// CP3: traduce (pid, dir_logica) a dirección física global. Retorna:
//   TRADUCCION_OK        y deja la dir global en *dir_global_out
//   TRADUCCION_SEG_FAULT si el desplazamiento + tamanio se pasa del segmento
//   TRADUCCION_INEXISTENTE si el proceso o el segmento no existen
// Esquema (consigna): num_segmento = dir_logica / SEGMENT_MAX_SIZE;
//                     desplazamiento = dir_logica % SEGMENT_MAX_SIZE;
//                     fisica = base_del_segmento + desplazamiento.
int traducir_direccion(uint32_t pid, uint32_t dir_logica, uint32_t tamanio, uint32_t* dir_global_out) {
  int seg_max = config_get_int_value(config, "SEGMENT_MAX_SIZE");
  uint32_t num_segmento = dir_logica / seg_max;
  uint32_t desplazamiento = dir_logica % seg_max;

  t_segmento* segmento = obtener_segmento(pid, num_segmento);
  if (segmento == NULL) return TRADUCCION_INEXISTENTE;

  if (desplazamiento + tamanio > segmento->tamanio) return TRADUCCION_SEG_FAULT;

  *dir_global_out = segmento->base + desplazamiento;
  return TRADUCCION_OK;
}

void inicializar_administrador_procesos(void) {
  administrador.procesos_por_pid = dictionary_create();
}

static char* pid_to_key(uint32_t pid) {
  return string_itoa(pid);
}

static t_contexto* crear_contexto_inicial(void) {
  t_contexto* contexto = malloc(sizeof(t_contexto));

  contexto->registros.pc = 0;

  contexto->registros.ax = 0;
  contexto->registros.bx = 0;
  contexto->registros.cx = 0;
  contexto->registros.dx = 0;

  contexto->registros.eax = 0;
  contexto->registros.ebx = 0;
  contexto->registros.ecx = 0;
  contexto->registros.edx = 0;

  contexto->registros.si = 0;
  contexto->registros.di = 0;

  contexto->tabla_segmentos = list_create();

  return contexto;
}

bool crear_proceso(uint32_t pid, char*path){
  char* key = pid_to_key(pid);

  if (dictionary_has_key(administrador.procesos_por_pid, key)) {
    free(key);
    log_warning(logger, "## ERROR: LA PID %d CORRESPONDE A UN PROCESO YA EXISTENTE.", *key);
    return false;
  }

  t_proceso_memoria* proceso = malloc(sizeof(t_proceso_memoria));

  proceso->pid = pid;
  proceso->script_path = strdup(path);
  
  proceso->contexto = crear_contexto_inicial();

  dictionary_put(administrador.procesos_por_pid, key, proceso);

  return true;
}

char*devolver_instruccion(uint32_t pc,char*lista_instrucciones){
    char*instruccion;
    int contador = 0; // NICO M: Según los ejemplos, el PC tomaría la primera linea de una lista de instrucciones como 1.
    char*copia_lista_instrucciones = string_duplicate(lista_instrucciones); // NICO M: CREO que string_split() rompe el string que se le pase. No queremos que la lista de instrucciones se rompa.
    char** tokenizado = string_split(copia_lista_instrucciones,"\n"); 
    free(copia_lista_instrucciones);
    do
    {
        instruccion = tokenizado[contador-1];
        contador++;
    } while (contador-1 != pc && tokenizado[contador-1] != NULL); // NICO M: Nos movemos por el array tokenizado hasta donde nos indique el PC, siempre y cuando no nos encontremos en un espacio inválido, lo que indicaría que el PC se sale del rango de la lista.
    string_array_destroy(tokenizado); // NICO M: Eliminamos el tokenizado, para liberar memoria.

    return instruccion;
}

void*manejar_proceso(void*arg){
  t_args_proceso*args = (t_args_proceso*) arg;
  int fd_cpu = args->fd_cpu;
  t_proceso_memoria*proceso = args->proceso;

  char * instrucciones = proceso->script_path;
  log_info(logger, "## PID: %d - Imprimiendo lista de instrucciones para el proceso...", proceso->pid);
  log_info(logger,instrucciones);

  while (proceso->contexto->proximo_a_detener){
    if (esperar_pedido_de_instruccion(fd_cpu)){
      uint32_t pc = recibir_pc(fd_cpu);

      char*proxima_instruccion = devolver_instruccion(pc, instrucciones);

      if (proxima_instruccion == NULL)
      {
        log_error(logger, "## PID: %d - Obtener instruccion: %d - INSTRUCCION FUERA DE RANGO.", proceso->pid, pc);
        enviar_confirmacion_a_CPU(fd_cpu,false);

      }
      else
      {
        log_info(logger,"## PID: %d - Obtener instrucción: %d - Instrucción: %s", proceso->pid,pc,proxima_instruccion);
        enviar_confirmacion_a_CPU(fd_cpu,true);
        enviar_proxima_instruccion_a_cpu(fd_cpu,proxima_instruccion);
      }
    }
  }
  destruir_proceso(proceso->pid);
  int*returnval = malloc(sizeof(1));
  pthread_exit(returnval);
}

bool inicializar_proceso(uint32_t pid, int fd_cpu) {
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = malloc(sizeof(t_proceso_memoria));

  proceso = dictionary_get(administrador.procesos_por_pid, key);

  t_args_proceso*args = malloc(sizeof(t_args_proceso));
  args->fd_cpu = fd_cpu;
  args->proceso = proceso;
  pthread_t nuevo_poceso;
  pthread_create(&nuevo_poceso, NULL, manejar_proceso,args);
  free(args);

  enviar_contexto_ejecucion_a_cpu(fd_cpu, *proceso->contexto);

  free(key);
  return true;
}

bool existe_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  bool existe = dictionary_has_key(administrador.procesos_por_pid, key);

  free(key);
  return existe;
}

t_proceso_memoria* obtener_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = dictionary_get(administrador.procesos_por_pid, key);

  free(key);
  return proceso;
}

t_segmento* obtener_segmento(uint32_t pid, uint32_t id_segmento) {
  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return NULL;
  }

  for (int i = 0; i < list_size(proceso->contexto->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->contexto->tabla_segmentos, i);

    if (segmento->id_segmento == id_segmento) {
      return segmento;
    }
  }

  return NULL;
}

t_resultado_crear_segmento crear_segmento(uint32_t pid, uint32_t id_segmento, uint32_t tamanio) {
  if (tamanio == 0) {
    return CREAR_SEGMENTO_TAMANIO_INVALIDO;
  }

  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return CREAR_SEGMENTO_PROCESO_INEXISTENTE;
  }

  if (obtener_segmento(pid, id_segmento) != NULL) {
    return CREAR_SEGMENTO_ID_REPETIDO;
  }

  if (!hay_hueco_contiguo(tamanio)) {
    if (requiere_compactacion(tamanio)) {
      return CREAR_SEGMENTO_REQUIERE_COMPACTACION;
    }
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  uint32_t base = reservar_espacio(tamanio);

  if (base == UINT32_MAX) {
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  t_segmento* segmento = malloc(sizeof(t_segmento));

  segmento->id_segmento = id_segmento;
  segmento->base = base;
  segmento->tamanio = tamanio;

  list_add(proceso->contexto->tabla_segmentos, segmento);

  return CREAR_SEGMENTO_OK;
}

t_list* obtener_todos_los_segmentos(void) {
  t_list* segmentos_ocupados = list_create();
  t_list* pids = dictionary_keys(administrador.procesos_por_pid); // Obtenemos una lista de los pids de los procesos.

  for (int i = 0; i < list_size(pids); i++) { // Por cada proceso indexado en el diccionario de procesos...
    char* key = list_get(pids, i);
    t_proceso_memoria* proceso = dictionary_get(administrador.procesos_por_pid, key); // Obtenemos un puntero al proceso correspondiente a cada pid.

    for (int j = 0; j < list_size(proceso->contexto->tabla_segmentos); j++) {  // Por cada segmento de la tabla de segmentos de cada proceso...
      t_segmento* segmento = list_get(proceso->contexto->tabla_segmentos, j);

      t_segmento_ocupado* ocupado = malloc(sizeof(t_segmento_ocupado));
      ocupado->proceso = proceso; // Indexamos un puntero al segmento y otro al proceso al que pertenece.
      ocupado->segmento = segmento;

      list_add(segmentos_ocupados, ocupado); // Añadimos el indice del segmento a la lista.
    }
  }

  list_destroy(pids); // Eliminamos de memoria la lista de pids pero no liberamos sus elementos.
  return segmentos_ocupados; // Devolvemos la lista de cada segmento ocupado en memoria.
}

static void destruir_segmento(void* elemento) {
  t_segmento* segmento = elemento;
  free(segmento);
}

static void destruir_contexto(t_contexto* contexto) {
  free(contexto);
}

static void destruir_proceso_memoria(void* elemento) {
  t_proceso_memoria* proceso = elemento;

  free(proceso->script_path);

  list_destroy_and_destroy_elements(proceso->contexto->tabla_segmentos, destruir_segmento);

  destruir_contexto(proceso->contexto);

  free(proceso);
}

bool destruir_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = dictionary_remove(administrador.procesos_por_pid, key);

  free(key);

  if (proceso == NULL) return false;

  for (int i = 0; i < list_size(proceso->contexto->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->contexto->tabla_segmentos, i);
    liberar_espacio(segmento->base, segmento->tamanio);
  }

  destruir_proceso_memoria(proceso);

  return true;
}

bool eliminar_segmento(uint32_t pid, uint32_t id_segmento)
{
  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return false;
  }

  for (int i = 0; i < list_size(proceso->contexto->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->contexto->tabla_segmentos, i);

    if (segmento->id_segmento == id_segmento) {
      list_remove(proceso->contexto->tabla_segmentos, i);

      liberar_espacio(segmento->base, segmento->tamanio);
      free(segmento);

      return true;
    }
  }

  return false;
}


void destruir_segmento_ocupado(void* elemento)
{
  free(elemento);
}



void destruir_administrador_procesos(void) {
  dictionary_destroy_and_destroy_elements( administrador.procesos_por_pid, destruir_proceso_memoria);
  administrador.procesos_por_pid = NULL;
}