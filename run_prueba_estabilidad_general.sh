#!/bin/bash
# ==============================================================================
#  Prueba Estabilidad General (PDF Pág. 10)
#  Prueba agregando CPU en tiempo de ejecución (Hot-Swapping / Multi-CPU)
#  Orden: KM -> KS -> MS -> IO -> SWAP -> CPU 1 -> CPU 2 (dinámica)
# ==============================================================================

set -e

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

SCRIPT_ESTABILIDAD="${1:-PLANI_PRE_3.prc}"

echo ""
echo "========================================="
echo "  INICIANDO PRUEBA ESTABILIDAD GENERAL ($SCRIPT_ESTABILIDAD)"
echo "========================================="

# 1. Kernel Memory
echo "[1/7] Iniciando Kernel Memory..."
./kernel_memory/bin/kernel_memory config/pruebaBase/KernelMemory.config &
PID_KM=$!
sleep 1

# 2. Kernel Scheduler
echo "[2/7] Iniciando Kernel Scheduler con proceso inicial: $SCRIPT_ESTABILIDAD..."
./kernel_scheduler/bin/kernel_scheduler config/pruebaBase/KernelScheduler.config "$SCRIPT_ESTABILIDAD" &
PID_KS=$!
sleep 1

# 3. Memory Stick 1 (256 bytes)
echo "[3/7] Iniciando Memory Stick 1 (256 bytes)..."
./memory_stick/bin/memory_stick config/pruebaBase/MemoryStick_1.config 256 &
PID_MS1=$!
sleep 1

# 4. IO
echo "[4/7] Iniciando módulos IO (STDIN, STDOUT, SLEEP)..."
./io/bin/io io/io.config STDIN &
PID_IO1=$!
./io/bin/io io/io.config STDOUT &
PID_IO2=$!
./io/bin/io io/io.config SLEEP &
PID_IO3=$!
sleep 1

# 5. SWAP
echo "[5/7] Iniciando SWAP..."
./swap/bin/swap swap/swap.config &
PID_SWAP=$!
sleep 1

# 6. CPU 1
echo "[6/7] Iniciando CPU 1..."
./cpu/bin/cpu cpu/cpu.config 1 &
PID_CPU1=$!
sleep 3

# 7. CPU 2 (Agregado dinámicamente en caliente)
echo "[7/7] AGREGANDO CPU 2 EN TIEMPO DE EJECUCIÓN (Hot-Swapping)..."
./cpu/bin/cpu cpu/cpu.config 2 &
PID_CPU2=$!
sleep 1

echo ""
echo "========================================="
echo "  PRUEBA DE ESTABILIDAD GENERAL CON 2 CPUS EN EJECUCIÓN"
echo "========================================="
echo "Presione CTRL+C para finalizar todos los módulos..."

cleanup() {
    echo ""
    echo "Cerrando todos los módulos..."
    kill $PID_KM $PID_KS $PID_MS1 $PID_IO1 $PID_IO2 $PID_IO3 $PID_SWAP $PID_CPU1 $PID_CPU2 2>/dev/null || true
    pkill -9 -f "bin/" 2>/dev/null || true
    echo "Prueba finalizada."
}

trap cleanup EXIT INT TERM
wait
