#!/bin/bash
# ==============================================================================
#  Prueba Base (PDF Pág. 5)
# ==============================================================================

set -e

# Directorio del repositorio
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

echo "========================================="
echo "  RECOMPILANDO TODO EL PROYECTO"
echo "========================================="
make -C utils clean && make -C utils
make -C kernel_memory clean && make -C kernel_memory
make -C kernel_scheduler clean && make -C kernel_scheduler
make -C cpu clean && make -C cpu
make -C memory_stick clean && make -C memory_stick
make -C swap clean && make -C swap
make -C io clean && make -C io

echo ""
echo "========================================="
echo "  FINALIZANDO INSTANCIAS ANTERIORES"
echo "========================================="
pkill -9 -f "bin/kernel_memory" || true
pkill -9 -f "bin/kernel_scheduler" || true
pkill -9 -f "bin/cpu" || true
pkill -9 -f "bin/memory_stick" || true
pkill -9 -f "bin/swap" || true
pkill -9 -f "bin/io" || true
sleep 1

SCRIPT_INICIAL="${1:-pruebas/PLANI_PRE_0.prc}"

echo ""
echo "========================================="
echo "  INICIANDO PRUEBA BASE ($SCRIPT_INICIAL)"
echo "========================================="

# 1. SWAP
echo "[1/6] Iniciando SWAP..."
./swap/bin/swap swap/swap.config &
PID_SWAP=$!
sleep 1

# 2. Kernel Memory
echo "[2/6] Iniciando Kernel Memory..."
./kernel_memory/bin/kernel_memory config/pruebaBase/KernelMemory.config &
PID_KM=$!
sleep 1

# 3. Memory Stick 1 (256 bytes)
echo "[3/6] Iniciando Memory Stick 1 (256 bytes)..."
./memory_stick/bin/memory_stick config/pruebaBase/MemoryStick_1.config 256 &
PID_MS1=$!
sleep 1

# 4. Kernel Scheduler
echo "[4/6] Iniciando Kernel Scheduler con script: $SCRIPT_INICIAL..."
./kernel_scheduler/bin/kernel_scheduler config/pruebaBase/KernelScheduler.config "$SCRIPT_INICIAL" &
PID_KS=$!
sleep 1

# 5. CPU 1
echo "[5/6] Iniciando CPU 1..."
./cpu/bin/cpu cpu/cpu.config 1 &
PID_CPU1=$!
sleep 1

# 6. IO
echo "[6/6] Iniciando módulos IO (STDIN, STDOUT, SLEEP)..."
./io/bin/io io/io.config STDIN &
PID_IO1=$!
./io/bin/io io/io.config STDOUT &
PID_IO2=$!
./io/bin/io io/io.config SLEEP &
PID_IO3=$!

echo ""
echo "========================================="
echo "  TODOS LOS MÓDULOS DE PRUEBA BASE EN EJECUCIÓN"
echo "========================================="
echo "Presione CTRL+C para finalizar todos los módulos..."

cleanup() {
    echo ""
    echo "Cerrando todos los módulos..."
    kill $PID_SWAP $PID_KM $PID_MS1 $PID_KS $PID_CPU1 $PID_IO1 $PID_IO2 $PID_IO3 2>/dev/null || true
    pkill -9 -f "bin/" 2>/dev/null || true
    echo "Prueba finalizada."
}

trap cleanup EXIT INT TERM
wait
