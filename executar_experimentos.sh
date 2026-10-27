#!/bin/sh
set -eu

BIN=${1:-./barbeiro_dorminhoco}
CSV=${2:-resultados_experimentos.csv}
rm -f "$CSV"

run_config() {
    exp=$1 config=$2 intervalo=$3 cadeiras=$4
    repeticao=1
    while [ "$repeticao" -le 5 ]; do
        "$BIN" "$intervalo" "$cadeiras" "$CSV" "$exp" "$config" "$repeticao"
        repeticao=$((repeticao + 1))
    done
}

run_config A A1 2 1
run_config A A2_B2 2 4
run_config A A3 2 8
run_config B B1 0.5 4
run_config B B3 5 4

printf 'Resultados gravados em %s\n' "$CSV"
