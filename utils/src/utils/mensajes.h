#ifndef MENSAJES_H
#define MENSAJES_H

// ============================================================
//  mensajes.h
//  Códigos de operación para identificar cada tipo de mensaje.
//  Incluir en cualquier módulo con: #include "mensajes.h"
// ============================================================

// Cada vez que un módulo se conecta a otro, manda este código
// para identificarse. El servidor lee el código y sabe quién se
// conectó y qué hacer con esa conexión.
//
// IMPORTANTE: nunca cambiar los valores numéricos una vez que
// el grupo empiece a probar, porque todos los módulos tienen
// que estar de acuerdo en qué número significa qué.
// NM: Al momento de mandar estos handshakes por mensaje, se requiere declarar una variable op_code y asociarla a alguno de estos. Si no se hace, el op_code no se pasa correctamente. NO olvidar el & en el argumento de enviar mensaje.
// ============================================================

typedef enum {
    // ── Handshakes de conexión ──────────────────────────────
    MSG_HANDSHAKE_KS    = 1,  // Kernel Scheduler se conecta a KM
    MSG_HANDSHAKE_CPU   = 2,  // CPU se conecta a KS, KM o MS
    MSG_HANDSHAKE_IO    = 3,  // IO se conecta a KS
    MSG_HANDSHAKE_MS    = 4,  // Memory Stick se conecta a KM
    MSG_HANDSHAKE_SWAP  = 5,  // SWAP se conecta a KM

    // ── Respuestas generales ─────────────────────────────────
    MSG_OK              = 10, // Confirmación genérica de que todo salió bien
    MSG_ERROR           = 11, // Algo salió mal
    MSG_DONE            = 21, // Terminó su tarea

    // ── (Acá van agregando los mensajes reales en CP2/CP3) ───

    // ── CP2 ──────────────────────────────────────────────────
    MSG_FETCH_CPU = 12, // CPU le pide instruccion a KM
    MSG_CONTEXTO_EJECUCION_KM = 13, // KM le envía contexto a CPU
    MSG_CONTEXTO_EJECUCION_CPU = 22,
    MSG_INIT_CPU = 14, // CPU ordena a KM crear un proceso
    MSG_REQUEST_PID = 15, // CPU pide PID a KS
    MSG_INTERRUPT = 16, // interrupciones hacia CPU
    MSG_INTERRUPCION_ATENDIDA = 17,
        //--- IO CP2
    MSG_STDIN = 18, // KS le pide a IO que lea por teclado
    MSG_STDOUT = 19, // KS le pide a IO que imprima por pantalla
    MSG_SLEEP = 20, // KS le pide a IO que se pause

    // MSG_DONTWAIT Lo saco, es una cte ya definida en <sys/socket.h> (hay que incluirla al usarla)

    // ── CP3 ──────────────────────────────────────────────────
    // 21 usado en MSG_DONE
    // 22 usado en MSG_CONTEXTO_EJECUCION_CPU
    MSG_READ = 23, // Memory Stick cuando CPU le pide leer algo en memoria
    MSG_WRITE = 24, // Memory Stick cuando CPU le pide escribir algo en memoria
    MSG_MUTEX_CREATE = 25,
    MSG_MUTEX_LOCK = 26,
    MSG_MUTEX_UNLOCK = 27,

    MSG_MEMORY_STICK_CONECTADA = 28, // KM cuando le informa a KS de una nueva Memory Stick conectada.
    MSG_MEMORIA_CORRUPTA = 29, // KM cuando le informa a KS de una corrupción de memoria debido a una desconexión de un Memory Stick.
    MSG_SOLICITAR_DESALOJO = 30, // KM cuando le pide a KS que desaloje CPUs para realizar compactación.
    MSG_DESALOJO_REALIZADO = 31, // KS Le informa a KM que desalojó las CPUs.
    
} op_code;

#endif // MENSAJES_H