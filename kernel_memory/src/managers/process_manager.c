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


char*generar_lista_instrucciones(char*path){
  log_info(logger, "Abriendo el archivo...");
  FILE*archivo = fopen(path, "r");
  if (archivo == NULL){
    log_error(logger, "Ha ocurrido un error al abrir el archivo.");
  }
  log_info(logger, "Posicionandose al fin del archivo...");
  fseek(archivo, 0, SEEK_END);
  log_info(logger, "Determinando tamaño del archivo");
  long tamanio = ftell(archivo);
  rewind(archivo);
  log_info(logger, "Alojando memoria para la lista de instrucciones...");
  char *lista_instrucciones = malloc(tamanio + 1);
  if (lista_instrucciones == NULL) {
      fclose(archivo);
      return NULL;
  }
  log_info(logger, "Escribiendo la lista de instrucciones...");
  size_t bytes_leidos = fread(lista_instrucciones, 1, tamanio, archivo);
  lista_instrucciones[bytes_leidos] = '\0';
  log_info(logger, "Cerrando archivo...");
  fclose(archivo);

  return lista_instrucciones;
}

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

  contexto->proximo_a_detener = false;

  return contexto;
}

bool crear_proceso(uint32_t pid, char*path){
  char* key = pid_to_key(pid);

  if (dictionary_has_key(administrador.procesos_por_pid, key)) {
    free(key);
    log_warning(logger, "## ERROR: LA PID %d CORRESPONDE A UN PROCESO YA EXISTENTE.", *key);
    return false;
  }

  log_info(logger, "El PID pedido está libre, alojando memoria...");
  t_proceso_memoria* proceso = malloc(sizeof(t_proceso_memoria));

  proceso->pid = pid;
  log_info(logger, "Generando lista de instrucciones...");
  char*lista_instrucciones = generar_lista_instrucciones(path);
  proceso->lista_instrucciones = string_duplicate(lista_instrucciones);
  free(lista_instrucciones);
  
  log_info(logger, "Creando contexto inicial...");
  proceso->contexto = crear_contexto_inicial();
  if (proceso->contexto==NULL){
    log_error(logger,"## ERROR: No se pudo crear el contexto inicial.");
    return false;
  }

  log_info(logger, "Indexando proceso en el diccionario.");
  dictionary_put(administrador.procesos_por_pid, key, proceso);

  return true;
}

char*devolver_instruccion(uint32_t pc,char*lista_instrucciones){

    char*instruccion;
    int contador = 0; // NICO M: Según los ejemplos, el PC tomaría la primera linea de una lista de instrucciones como 1.
    char*copia_lista_instrucciones = string_duplicate(lista_instrucciones); // NICO M: CREO que string_split() rompe el string que se le pase. No queremos que la lista de instrucciones se rompa.
    char** tokenizado = string_split(copia_lista_instrucciones,"\n"); 
    do
    {
        instruccion = string_duplicate(tokenizado[contador]);
        // log_info(logger, "Instruccion %d: %s",contador, instruccion);
        contador++;
    } while (contador-1 != pc && instruccion != NULL); // NICO M: Nos movemos por el array tokenizado hasta donde nos indique el PC, siempre y cuando no nos encontremos en un espacio inválido, lo que indicaría que el PC se sale del rango de la lista.
    free(copia_lista_instrucciones);
    if (tokenizado[contador-1] == NULL){
      string_array_destroy(tokenizado);
      return NULL;
    }
    string_array_destroy(tokenizado); // NICO M: Eliminamos el tokenizado, para liberar memoria.
    return instruccion;
}

void*manejar_proceso(void*arg){
  t_args_proceso*args = (t_args_proceso*) arg;
  log_info(logger, "Comenzando manejo del proceso de PID %d.", args->proceso->pid);
  int fd_cpu = args->fd_cpu;
  t_proceso_memoria*proceso = args->proceso;

  char * instrucciones = proceso->lista_instrucciones;
  log_info(logger, "## PID: %d - Imprimiendo lista de instrucciones para el proceso...", proceso->pid);
  log_info(logger,instrucciones);

  bool interrumpido = false;

  while (!interrumpido){
    op_code*codigo = esperar_pedido_de_instruccion(fd_cpu);
    if (*codigo == MSG_FETCH_CPU){
      uint32_t pc = recibir_pc(fd_cpu);
      log_info(logger, "## PID: %d - Recibido PC: %d.", proceso->pid, pc);
      char*proxima_instruccion = devolver_instruccion(pc, instrucciones);
      log_info(logger, "Busqueda de instrucción concluida.");
      if (proxima_instruccion == NULL)
      {
        log_error(logger, "## PID: %d - Obtener instruccion: %d - INSTRUCCION FUERA DE RANGO.", proceso->pid, pc);
        enviar_confirmacion_a_CPU(fd_cpu,false);
        free(proxima_instruccion);
      }
      else
      {
        log_info(logger,"## PID: %d - Obtener instrucción: %d - Instrucción: %s", proceso->pid,pc,proxima_instruccion);
        enviar_confirmacion_a_CPU(fd_cpu,true);
        enviar_proxima_instruccion_a_cpu(fd_cpu,proxima_instruccion);
        free(proxima_instruccion);
      } 
    }
    if (*codigo == MSG_INTERRUPT) interrumpido = true;
  }
  if (proceso->contexto->proximo_a_detener) {
    log_info(logger, "## PID: %d - Proceso eliminado.",proceso -> pid);
    destruir_proceso(proceso->pid);
  }

  // NICO M: Acá debería volver a atender mensajes de INIT_PROC_CPU, por lo que debería iniciar atender_mensajes_cpu() de nuevo. El problema es que si lo llamo a secas, no puedo cerrar el thread porque sino dejo de atender esa función.
  // free(arg);
  int*returnval = malloc(sizeof(1));
  pthread_exit(returnval);
}

bool inicializar_proceso(uint32_t pid, int fd_cpu) {
  
  log_info(logger, "PID recibido desde CPU: %u", pid);
  
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = dictionary_get(administrador.procesos_por_pid, key);

  // Validaciones del proceso creado
  if (proceso == NULL) {
    log_error(
        logger,
        "No se encontró el proceso con PID %u",
        pid
  );
    return false;
  }
  if (proceso->contexto == NULL) {
      log_error(
          logger,
          "El proceso con PID %u no tiene contexto",
          pid
    );
    return false;
  }

  t_args_proceso*args = malloc(sizeof(t_args_proceso));
  args->fd_cpu = fd_cpu;
  args->proceso = proceso;
  pthread_t nuevo_proceso;
  pthread_create(&nuevo_proceso, NULL, manejar_proceso,args);
  // free(args);

  log_info(logger, "Proceso creado");

  int tamanio_buffer =0 ;
  void*buffer = serializar_contexto_inicial(proceso->contexto, &tamanio_buffer);

  if (buffer==NULL)
  {
    log_error(logger, "## ERROR: Ha ocurrido un error al serializar el contexto inicial.");
  }
  
  log_info(logger, "se creo el contexto y se va a enviar contexto");

  enviar_contexto_ejecucion_a_cpu(fd_cpu, buffer, tamanio_buffer);
  log_info(logger, "se envio contexto");

  free(buffer);
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

  free(proceso->lista_instrucciones);

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

bool actualizar_contexto(uint32_t p,t_contexto*contexto){
  uint32_t pid = p;
  char*key = pid_to_key(pid);
  
  if (!existe_proceso(pid)){
    log_error(logger, "## ERROR: EL PID %d NO CORRESPONDE A UN PROCESO REGISTRADO", pid);
    return false;
  }

  t_proceso_memoria *proceso = dictionary_get(administrador.procesos_por_pid, key);

  proceso->contexto = contexto;

  return true;

}