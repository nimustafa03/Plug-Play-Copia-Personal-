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

    // ── (Acá van agregando los mensajes reales en CP2/CP3) ───

    // ── CP2 ──────────────────────────────────────────────────
    MSG_FETCH_CPU = 12, // CPU le pide instruccion a KM
    MSG_CONTEXTO_EJECUCION_KM = 13, // KM le envía contexto a CPU
    MSG_INIT_CPU = 14 // CPU ordena a KM crear un proceso
} op_code;

#endif // MENSAJES_H