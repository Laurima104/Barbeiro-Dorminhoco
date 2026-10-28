#!/bin/sh
set -eu

BIN=${1:-./barbeiro_dorminhoco}
CSV=${2:-resultados_experimentos.csv}
TRACE=${3:-eventos_experimentos.csv}
MEDIA=${4:-media_resultados_experimentos.csv}
rm -f "$CSV" "$TRACE" "$MEDIA"

for cadeiras in 1 4 8; do
    for intervalo in 0.5 2 5; do
        config="C${cadeiras}_I${intervalo}ms"
        repeticao=1
        while [ "$repeticao" -le 5 ]; do
            TRACE_CSV="$TRACE" "$BIN" "$intervalo" "$cadeiras" "$CSV" "MATRIZ_3x3" "$config" "$repeticao"
            repeticao=$((repeticao + 1))
        done
    done
done

python_cmd=${PYTHON:-}
if [ -z "$python_cmd" ]; then
    if command -v python3 >/dev/null 2>&1; then
        python_cmd=python3
    elif [ -x "$HOME/.venvs/matplotlib/bin/python" ]; then
        python_cmd="$HOME/.venvs/matplotlib/bin/python"
    else
        echo "Python não encontrado. Execute gerar_medias.py manualmente com Python 3." >&2
        exit 1
    fi
fi
"$python_cmd" gerar_medias.py "$CSV" "$MEDIA"
printf 'Resultados brutos: %s\nMédias por combinação: %s\nEventos: %s\n' "$CSV" "$MEDIA" "$TRACE"
