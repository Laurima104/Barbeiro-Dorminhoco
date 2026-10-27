#!/usr/bin/env python3
"""Gera comparações a partir do CSV real produzido por executar_experimentos.sh."""
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ImportError as exc:
    raise SystemExit("Instale matplotlib para gerar os gráficos: python3 -m pip install matplotlib") from exc


def numero(valor):
    return float(valor) if valor not in ("", "N/A") else math.nan


def agrega(linhas, categoria, metrica, experimento):
    valores = defaultdict(list)
    for linha in linhas:
        referencia_compartilhada = experimento == "B" and linha["configuracao"] == "A2_B2"
        if linha["experimento"] != experimento and not referencia_compartilhada:
            continue
        x, y = numero(linha[categoria]), numero(linha[metrica])
        if math.isfinite(x) and math.isfinite(y):
            valores[x].append(y)
    xs = sorted(valores)
    medias = [statistics.mean(valores[x]) for x in xs]
    erros = [statistics.stdev(valores[x]) if len(valores[x]) > 1 else 0.0 for x in xs]
    return xs, medias, erros


if len(sys.argv) != 2:
    raise SystemExit(f"Uso: {sys.argv[0]} resultados_experimentos.csv")
with open(sys.argv[1], newline="", encoding="utf-8") as arquivo:
    dados = list(csv.DictReader(arquivo))
if not dados:
    raise SystemExit("CSV sem resultados para plotar")

graficos = [
    ("A", "cadeiras", "espera_media_ms", "Cadeiras de espera", "Espera média (ms)", "cadeiras_vs_espera.png"),
    ("A", "cadeiras", "vazao_clientes_s", "Cadeiras de espera", "Vazão (clientes/s)", "cadeiras_vs_vazao.png"),
    ("B", "intervalo_chegada_ms", "taxa_rejeicao_pct", "Intervalo entre chegadas (ms)", "Rejeição (%)", "intervalo_vs_rejeicao.png"),
    ("B", "intervalo_chegada_ms", "utilizacao_pct", "Intervalo entre chegadas (ms)", "Utilização do barbeiro (%)", "intervalo_vs_utilizacao.png"),
]
for experimento, coluna_x, coluna_y, rotulo_x, rotulo_y, arquivo in graficos:
    xs, medias, erros = agrega(dados, coluna_x, coluna_y, experimento)
    plt.figure(figsize=(7, 4.5))
    plt.errorbar(xs, medias, yerr=erros, marker="o", capsize=4)
    plt.xlabel(rotulo_x)
    plt.ylabel(rotulo_y)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(arquivo, dpi=160)
    plt.close()
    print(f"Gerado: {arquivo}")
