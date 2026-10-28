#!/bin/sh
set -eu

BIN=${1:-./barbeiro_dorminhoco}
CSV=${2:-resultados_experimentos.csv}
TRACE=${3:-eventos_experimentos.csv}
MEDIA=${4:-media_resultados_experimentos.csv}
TIMEOUT_SECONDS=${TIMEOUT_SECONDS:-10}
rm -f "$CSV" "$TRACE" "$MEDIA"

if ! command -v timeout >/dev/null 2>&1; then
    echo "O comando timeout do GNU coreutils é necessário para limitar cada execução." >&2
    exit 1
fi
if command -v python3 >/dev/null 2>&1; then
    python_cmd=${PYTHON:-python3}
elif [ -x "$HOME/.venvs/matplotlib/bin/python" ]; then
    python_cmd=${PYTHON:-"$HOME/.venvs/matplotlib/bin/python"}
else
    echo "Python 3 não encontrado; é necessário para agregar médias e falhas." >&2
    exit 1
fi

for cadeiras in 1 4 8; do
    for intervalo in 0.5 2 5; do
        config="C${cadeiras}_I${intervalo}ms"
        repeticao=1
        while [ "$repeticao" -le 5 ]; do
            if SIM_TIMEOUT_S="$TIMEOUT_SECONDS" timeout --signal=TERM --kill-after=2s "${TIMEOUT_SECONDS}s" \
                env BARBEIRO_CLIENTES=200 TRACE_CSV="$TRACE" "$BIN" "$intervalo" "$cadeiras" \
                "$CSV" "MATRIZ_3x3" "$config" "$repeticao"; then
                :
            else
                rc=$?
                if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
                    tipo=TIMEOUT
                else
                    tipo=ERRO
                fi
                "$python_cmd" registrar_falha.py "$CSV" "$TRACE" "MATRIZ_3x3" "$config" \
                    "$repeticao" "$intervalo" "$cadeiras" "$TIMEOUT_SECONDS" "$tipo"
            fi
            repeticao=$((repeticao + 1))
        done
    done
done

"$python_cmd" gerar_medias.py "$CSV" "$MEDIA"
printf 'Resultados brutos: %s\nMédias por combinação: %s\nEventos: %s\n' "$CSV" "$MEDIA" "$TRACE"
