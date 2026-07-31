#!/bin/bash
# ==============================================================================
#  Ejecutor de TODAS LAS PRUEBAS (Secuencial)
#  Incluye: Base (Parte 1 y 2), Corto Plazo, Memoria, Mediano Plazo,
#           Herencia de Prioridades y Estabilidad General (Multi-CPU en caliente).
# ==============================================================================

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

hacer_pausa() {
    echo ""
    echo "================================================================="
    echo "  PRUEBA FINALIZADA / PAUSADA"
    echo "  Presione ENTER para continuar con la siguiente prueba..."
    echo "================================================================="
    read -r
}

echo "================================================================="
echo "  EJECUTANDO SECUENCIA COMPLETA DE PRUEBAS DEL TP"
echo "================================================================="
sleep 2

# 1. Prueba Base (Parte 1)
echo ">>> 1. PRUEBA BASE (PLANI_PRE_0.prc)"
./run_prueba_base.sh pruebas/PLANI_PRE_0.prc || true
hacer_pausa

# 2. Prueba Base (Parte 2)
echo ">>> 2. PRUEBA BASE (MEMORIA_PRE_0.prc)"
./run_prueba_base.sh pruebas/MEMORIA_PRE_0.prc || true
hacer_pausa

# 3. Prueba Corto Plazo
echo ">>> 3. PRUEBA PLANIFICACIÓN CORTO PLAZO (PCP.prc)"
./run_prueba_corto_plazo.sh || true
hacer_pausa

# 4. Prueba Memoria
echo ">>> 4. PRUEBA MEMORIA (PLANI_MEM.prc)"
./run_prueba_memoria.sh || true
hacer_pausa

# 5. Prueba Mediano Plazo
echo ">>> 5. PRUEBA PLANIFICACIÓN MEDIANO PLAZO (PMP.prc)"
./run_prueba_mediano_plazo.sh || true
hacer_pausa

# 6. Prueba Herencia de Prioridades
echo ">>> 6. PRUEBA HERENCIA DE PRIORIDADES (PHP.prc)"
./run_prueba_herencia_prioridades.sh || true
hacer_pausa

# 7. Prueba Estabilidad General (Agregado dinámico de CPU en caliente)
echo ">>> 7. PRUEBA ESTABILIDAD GENERAL (Multi-CPU en caliente)"
./run_prueba_estabilidad_general.sh || true

echo ""
echo "================================================================="
echo "  SECUENCIA DE PRUEBAS CONCLUIDA EXITOSAMENTE"
echo "================================================================="
