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
#include "../handlers/handler_swap.h"
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

static char* pid_to_key(uint32_t pid) {
  return string_itoa(pid);
}

bool desuspender_proceso(uint32_t pid)
{
  log_debug(logger, "Buscando proceso por pid...");
  t_proceso_memoria*proceso = obtener_proceso(pid);
  log_debug(logger, "Hay elementos en el espacio del diccionario consultado.");

  if (proceso == NULL || proceso->contexto == NULL) {
    log_error(logger,"## ERROR: El proceso no existe. PID: %d", pid);
    return false;
  }

  log_info(logger, "Se procederá a desuspender el proceso de PID. %d - Restaurando segmentos desde SWAP.", pid);
  
  for (int i = list_size(administrador.segmentos_guardados_en_swap)-1; i >=0; i--){
      t_segmento_swap*seg_swap = list_get(administrador.segmentos_guardados_en_swap,i);

      if (seg_swap->pid == pid){

        // reservar espacio en memoria fisica (RAM)
        uint32_t nueva_base = reservar_espacio(seg_swap->tamanio);
        if (nueva_base == UINT32_MAX)
        {
          log_error(logger, "## ERROR: Sin espacio en RAM para desuspender el proceso PID %u - El segmento %u tiene un tamaño mayor al permitido.", pid, seg_swap->id_segmento);
          return false;
        }

        //traer bloque desde swap
        int tamanio_bloque = swap_get_block_size();
        void*buffer_segmento = malloc(tamanio_bloque);

        if (!swap_leer_bloque(seg_swap->nro_bloque,buffer_segmento))
        {
          log_error(logger, "## ERROR: No se ha podido leer el bloque %d de SWAP", seg_swap->nro_bloque);
          liberar_espacio(nueva_base, seg_swap->tamanio); // rollback
          free(buffer_segmento);
          return false;
        }

        if (!escribir_memoria_fisica(nueva_base,seg_swap->tamanio,buffer_segmento))
        {
          log_error(logger, "## ERROR: No se ha podido escribir en memoria fisica para PID %u - Segmento %u", pid, seg_swap->id_segmento);
          liberar_espacio(nueva_base,seg_swap->tamanio);
          free(buffer_segmento);
          return false;
        }

        t_segmento*nuevo_segmento = malloc(sizeof(t_segmento));
        nuevo_segmento->id_segmento = seg_swap->id_segmento;
        nuevo_segmento->base = nueva_base;
        nuevo_segmento->tamanio = seg_swap->tamanio;

        list_add(proceso->contexto->tabla_segmentos,nuevo_segmento);

        liberar_bloque_swap(seg_swap->nro_bloque);
        list_remove(administrador.segmentos_guardados_en_swap,i);
        
        free(seg_swap);
        free(buffer_segmento);


      }
  }
  log_info(logger, "## PID: %u desuspendido exitosamente.", pid);
  return true;
}


static void destruir_segmento(void* elemento) {
  t_segmento* segmento = elemento;
  free(segmento);
}

bool suspender_proceso(uint32_t pid)
{
  t_proceso_memoria*proceso = obtener_proceso(pid);

  log_debug(logger, "Proceso obtenido. Verificando existencia y existencia de contexto.");
  if(proceso == NULL || proceso->contexto == NULL)
  {
    log_error(logger, "## ERROR: El proceso que se pretende suspender no existe.");
    return false;
  }

  log_debug(logger, "El proceso existe, revisando su lista de segmentos.");
  t_list*lista_segmentos =proceso->contexto->tabla_segmentos;
  int cantidad_segmentos = list_size(lista_segmentos);

  if (cantidad_segmentos == 0)
  {
    log_debug(logger, "El proceso no tiene segmentos asignados. No se requiere enviar nada a SWAP.");
    return true;
  }

  while (list_size(lista_segmentos)>0)
  {
    log_debug(logger,"Extrayendo segmento de la tabla...");
    t_segmento*segmento = list_remove(lista_segmentos,0);

    log_debug(logger, "Encontrando bloque libre para guardar...");
    int nro_bloque = encontrar_bloque_libre();

    log_debug(logger,"Consiguiendo tamaño de bloque de swap...");
    int tamanio_bloque = swap_get_block_size();

    log_debug(logger, "Alojando memoria...");
    void*buffer_segmento = calloc(1,tamanio_bloque);

    log_debug(logger, "Intentando leer a memoria física...");

    if(!leer_memoria_fisica(segmento->base,segmento->tamanio,buffer_segmento)){
      free(buffer_segmento);
      log_error(logger, "## ERROR: Ocurrió un error al leer memoria fisica para el segmento.");
      free(segmento);
      return false;
    }

    log_debug(logger, "Intentando escribir bloque en swap...");
    if(!swap_escribir_bloque(nro_bloque,buffer_segmento))
    {
      free(buffer_segmento);
      free(segmento);
      log_error(logger, "## ERROR: Ocurrió un error al escribir al bloque.");
      return false;
    }

    log_debug(logger, "Indexando el segmento en la lista de segmentos en swap...");
    t_segmento_swap*seg_swap = malloc(sizeof(t_segmento_swap));
    seg_swap->pid = pid;
    seg_swap->nro_bloque = nro_bloque;
    seg_swap->id_segmento = segmento->id_segmento;
    seg_swap->tamanio = segmento->tamanio;

    list_add(administrador.segmentos_guardados_en_swap,seg_swap);

    log_info(logger, "Se ha guardado un segmento en la swap. ID: %d, BLOQUE EN SWAP: %d.", segmento->id_segmento, nro_bloque);
    log_debug(logger, "Se liberará memoria. La memoria libre antes de la liberación es %u",obtener_memoria_libre_total());
    liberar_espacio(segmento->base,segmento->tamanio);
    destruir_segmento(segmento);
    log_info(logger, "Se ha eliminado el segmento de la RAM.");
    free(buffer_segmento);
    log_debug(logger, "La memoria libre total luego de la suspensión es: %u", obtener_memoria_libre_total());
  }
  return true;
}

void imprimir_lista_segmentos(uint32_t pid){
  char*key = pid_to_key(pid);
  t_proceso_memoria*proceso = dictionary_get(administrador.procesos_por_pid, key);
  free(key);

  t_list*lista_segmentos = proceso->contexto->tabla_segmentos;
  log_info(logger, "El tamaño de la lista de segmentos es el siguiente: %d", list_size(lista_segmentos));
  for (int i = 0; i < list_size(lista_segmentos); i++)
  {
    t_segmento*segmento = list_get(lista_segmentos, i);
    log_info(logger, "ID DEL SEGMENTO: %d", segmento->id_segmento);
    log_info(logger, "BASE DEL SEGMENTO: %d", segmento->base);
    log_info(logger, "TAMAÑO DEL SEGMENTO: %d", segmento->tamanio);
  }

  return;
}

char*generar_lista_instrucciones(char*path){
  log_info(logger, "Abriendo el archivo...");
  FILE*archivo = fopen(path, "r");
  if (archivo == NULL){
    log_error(logger, "Ha ocurrido un error al abrir el archivo.");
    return NULL;
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
  administrador.segmentos_guardados_en_swap = list_create();
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
    // se logueaba *key DESPUES del free(key) (-Wuse-after-free) y encima
    // con %d sobre un char*. Logueamos el pid y liberamos despues.
    log_warning(logger, "## ERROR: LA PID %u CORRESPONDE A UN PROCESO YA EXISTENTE.", pid);
    free(key);
    return false;
  }

  log_info(logger, "El PID pedido está libre, alojando memoria...");
  t_proceso_memoria* proceso = malloc(sizeof(t_proceso_memoria));

  proceso->pid = pid;
  log_info(logger, "Generando lista de instrucciones...");
  proceso->lista_instrucciones = generar_lista_instrucciones(path);
  if(!proceso->lista_instrucciones){
    log_error(logger, "## ERROR: No se pudo crear la lista de instrucciones.");
  }
  
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
  if (lista_instrucciones == NULL) return NULL;

  int contador = 0; // NICO M: Según los ejemplos, el PC tomaría la primera linea de una lista de instrucciones como 1.
  char*copia_lista_instrucciones = string_duplicate(lista_instrucciones); // NICO M: CREO que string_split() rompe el string que se le pase. No queremos que la lista de instrucciones se rompa.
  char** tokenizado = string_split(copia_lista_instrucciones,"\n"); 
  while (tokenizado[contador] != NULL)
  {
      contador++;
  } 
  char*instruccion = NULL;
  if (pc < contador){
    instruccion = string_duplicate(tokenizado[pc]);
  }
  string_array_destroy(tokenizado); // NICO M: Eliminamos el tokenizado, para liberar memoria.
  free(copia_lista_instrucciones);
  return instruccion;
}


void*manejar_proceso(void*arg){
  t_args_proceso*args = (t_args_proceso*) arg;
  uint32_t pid_local = args->proceso->pid;
  log_info(logger, "Comenzando manejo del proceso de PID %d.", pid_local);
  int fd_cpu = args->fd_cpu;
  t_proceso_memoria*proceso = args->proceso;

  char * instrucciones = proceso->lista_instrucciones;
  if (instrucciones != NULL){
    log_info(logger, "Instrucciones cargadas correctamente");
  }
  log_info(logger, "## PID: %d - Imprimiendo lista de instrucciones para el proceso...", pid_local);
  log_info(logger,"instrucciones: %s", instrucciones);

  
  bool interrumpido_local = false;

  while (!interrumpido_local && existe_proceso(pid_local)){
    t_proceso_memoria*proceso_actual = obtener_proceso(pid_local);
    if (proceso_actual == NULL || proceso_actual->contexto == NULL || proceso_actual->contexto->proximo_a_detener){
      break;
    }

    op_code*codigo = esperar_pedido_de_instruccion(fd_cpu);

    if (codigo == NULL){
      log_error(logger, "Se perdió la conexión con el socket CPU");
      break;
    }
    if (*codigo == MSG_FETCH_CPU){
      uint32_t pc = recibir_pc(fd_cpu);
      log_info(logger, "## PID: %d - Recibido PC: %d.", pid_local, pc);

      char*proxima_instruccion = devolver_instruccion(pc, instrucciones);
      log_info(logger, "Busqueda de instrucción concluida.");

      if (proxima_instruccion == NULL)
      {
        log_error(logger, "## PID: %d - Obtener instruccion: %d - INSTRUCCION FUERA DE RANGO.", pid_local, pc);
        enviar_confirmacion_a_CPU(fd_cpu,false);
        free(proxima_instruccion);
      }
      else
      {
        log_info(logger,"## PID: %d - Obtener instrucción: %d - Instrucción: %s", pid_local,pc,proxima_instruccion);
        enviar_confirmacion_a_CPU(fd_cpu,true);
        enviar_proxima_instruccion_a_cpu(fd_cpu,proxima_instruccion);

        proceso_actual = obtener_proceso(pid_local);

        if (proceso_actual->contexto != NULL) {notificar_segmentos_a_cpu(fd_cpu, proceso_actual->contexto);}
        free(proxima_instruccion);
      } 
    }

    if (*codigo == MSG_INTERRUPT || *codigo == MSG_EXIT_CPU || *codigo == MSG_SEG_FAULT) {
      log_info(logger, "Interrumpiendo proceso...");
      interrumpido_local = true;
      }
    free(codigo);
    // log_info(logger, "La tabla de segmentos del proceso PID %d es la siguiente.", pid_local);
    // imprimir_lista_segmentos(pid_local);
  }

  log_info(logger, "## PID: %d - Saliendo del ciclo de FETCH", pid_local);
  
  log_info(logger, "Liberando memoria...");
  free(args);
  return NULL;
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
    free(key);
    return false;
  }
  if (proceso->contexto == NULL) {
      log_error(
          logger,
          "El proceso con PID %u no tiene contexto",
          pid
      );
      free(key);
      return false;
  }

  log_info(logger, "Se va a enviar contexto a CPU en FD %d", fd_cpu);
  enviar_contexto_ejecucion_a_cpu(fd_cpu, proceso->contexto);
  log_info(logger, "Se envió contexto a CPU en FD %d", fd_cpu);

  t_args_proceso* args = malloc(sizeof(t_args_proceso));
  args->fd_cpu = fd_cpu;
  args->proceso = proceso;

  manejar_proceso(args);

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
  log_info(logger,"La memoria total disponible es: %u", obtener_memoria_libre_total());

  if (tamanio == 0) {
    log_error(logger, "## ERROR: El tamaño es inválido.");
    return CREAR_SEGMENTO_TAMANIO_INVALIDO;
  }

  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    log_error(logger, "## ERROR: Se intentó crear un segmento para un proceso inexistente.");
    return CREAR_SEGMENTO_PROCESO_INEXISTENTE;
  }

  if (obtener_segmento(pid, id_segmento) != NULL) {
    log_error(logger, "## ERROR: Se intentó crear un segmento con el mismo id que otro segmento.");
    return CREAR_SEGMENTO_ID_REPETIDO;
  }

  if (!hay_hueco_contiguo(tamanio)) {
    if (requiere_compactacion(tamanio)) {
      log_warning(logger, "El segmento requiere una compactación de memory stick para crearse.");
      return CREAR_SEGMENTO_REQUIERE_COMPACTACION;
    }
    log_error(logger, "## ERROR: No se requiere compactación, pero no hay suficiente memoria para crear el segmento.");
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  uint32_t base = reservar_espacio(tamanio);

  if (base == UINT32_MAX) {
    log_error(logger, "## ERROR: el tamaño pedido es mayor al maximo permitido para un uint32_t. No existe suficiente memoria para alojarlo.");
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  t_segmento* segmento = malloc(sizeof(t_segmento));

  segmento->id_segmento = id_segmento;
  segmento->base = base;
  segmento->tamanio = tamanio;

  log_info(logger, "El segmento se ha creado satisfactoriamente y ha sido agregado a la tabla de segmentos.");
  list_add(proceso->contexto->tabla_segmentos, segmento);
  imprimir_lista_segmentos(pid);
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


static void destruir_contexto(t_contexto* contexto) {
  free(contexto);
}

static void destruir_proceso_memoria(void* elemento) {
  t_proceso_memoria* proceso = elemento;

  free(proceso->lista_instrucciones);

  if (proceso->contexto->tabla_segmentos){
    list_destroy_and_destroy_elements(proceso->contexto->tabla_segmentos, destruir_segmento);
  }
  destruir_contexto(proceso->contexto);

  free(proceso);
}



bool destruir_proceso(uint32_t pid) {

  char* key = pid_to_key(pid);

  if (key == NULL) {
      log_error(
          logger,
          "PID: %u - No se pudo generar la clave",
          pid
      );

      return false;
  }

  t_proceso_memoria* proceso =
      dictionary_remove(
          administrador.procesos_por_pid,
          key
      );

  free(key);

  if (proceso == NULL) {
      log_warning(
          logger,
          "PID: %u - No existe el proceso a destruir",
          pid
      );

      return false;
  }

  if (proceso->contexto == NULL) {
      log_error(
          logger,
          "PID: %u - El proceso no tiene contexto",
          pid
      );

      destruir_proceso_memoria(proceso);
      return false;
  }

  if (proceso->contexto->tabla_segmentos == NULL) {
      log_error(
          logger,
          "PID: %u - El proceso no tiene tabla de segmentos",
          pid
      );

      destruir_proceso_memoria(proceso);
      return false;
  }

  int cantidad_segmentos =
      list_size(proceso->contexto->tabla_segmentos);

  log_info(
      logger,
      "PID: %u - Liberando %d segmentos",
      pid,
      cantidad_segmentos
  );

  for (int i = 0; i < cantidad_segmentos; i++) {
      t_segmento* segmento =
          list_get(proceso->contexto->tabla_segmentos, i);

      if (segmento == NULL) {
          log_warning(
              logger,
              "PID: %u - Segmento NULL en posición %d",
              pid,
              i
          );

          continue;
      }

      log_debug(
          logger,
          "PID: %u - Liberando segmento %u - Base: %u - Tamaño: %u",
          pid,
          segmento->id_segmento,
          segmento->base,
          segmento->tamanio
      );

      liberar_espacio(
          segmento->base,
          segmento->tamanio
      );
  }

  // Liberacion de segmentos guardados en swap

  for (int i = 0; i<list_size(administrador.segmentos_guardados_en_swap); i++)
  {
    t_segmento_swap*seg_swap = list_get(administrador.segmentos_guardados_en_swap,i);
    if (seg_swap->pid == pid)
    {
      liberar_bloque_swap(seg_swap->nro_bloque);
      list_remove(administrador.segmentos_guardados_en_swap,i);
      free(seg_swap);
      i--;
    }
  }

  log_debug(
      logger,
      "PID: %u - Destruyendo estructuras del proceso",
      pid
  );

  destruir_proceso_memoria(proceso);

  log_info(
      logger,
      "PID: %u - Proceso destruido correctamente",
      pid
  );

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
   char* key = pid_to_key(p);
    if (key == NULL) {
        log_error(logger, "## ERROR: No se pudo generar la clave para el PID %u", p);
        return false;
    }

    t_proceso_memoria *proceso = dictionary_get(administrador.procesos_por_pid, key);
    free(key); // <--- Liberación indispensable para evitar fugas de memoria

    if (proceso == NULL || proceso->contexto == NULL) {
        log_warning(logger, "## WARNING: El PID %u ya no existe en el diccionario (fue destruido por el Scheduler).", p);
        return false;
    }

    // Opcional: Si el contexto anterior requiere ser liberado antes de reasignar
    if (proceso->contexto != NULL && proceso->contexto != contexto) {
        // destruir_contexto(proceso->contexto); // Activar si la CPU devuelve un contexto totalmente nuevo malloc'eado
    }
    proceso->contexto->registros = contexto->registros;
    proceso->contexto->proximo_a_detener = contexto->proximo_a_detener;

    if (contexto->tabla_segmentos != NULL) {
        list_destroy_and_destroy_elements(contexto->tabla_segmentos, free);
    }
    free(contexto);

    return true;
}