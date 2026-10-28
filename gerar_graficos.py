#!/usr/bin/env python3
"""Gera G1–G4 a partir dos CSVs de métricas e eventos da simulação."""
import csv
import math
import statistics
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


def carregar(caminho):
    with open(caminho, newline="", encoding="utf-8") as arquivo:
        return list(csv.DictReader(arquivo))


def num(valor):
    return float(valor) if valor not in ("", "N/A") else math.nan


def p95(valores):
    if not valores:
        return math.nan
    ordenados = sorted(valores)
    return ordenados[math.ceil(0.95 * len(ordenados)) - 1]


if len(sys.argv) not in (2, 3):
    raise SystemExit(f"Uso: {sys.argv[0]} resultados_experimentos.csv [eventos_experimentos.csv]")
metricas = carregar(sys.argv[1])
eventos = carregar(sys.argv[2]) if len(sys.argv) == 3 else carregar("eventos_experimentos.csv")

# G1: vazão por número de cadeiras, usando somente as execuções de 2 ms.
g1 = defaultdict(list)
for r in metricas:
    if r["experimento"] == "A":
        g1[int(r["cadeiras"])].append(num(r["vazao_clientes_s"]))
x = sorted(g1)
y = [statistics.mean(g1[c]) for c in x]
err = [statistics.stdev(g1[c]) if len(g1[c]) > 1 else 0 for c in x]
plt.figure(figsize=(7, 4.5))
plt.errorbar(x, y, yerr=err, marker="o", capsize=4)
plt.title("G1 — Cadeiras × clientes atendidos por segundo (intervalo de 2 ms)")
plt.xlabel("Cadeiras de espera")
plt.ylabel("Clientes atendidos/s")
plt.grid(True, alpha=.3)
plt.tight_layout()
plt.savefig("g1.png", dpi=160)
plt.close()

# G2: p95 de espera por intervalo, incluindo a referência compartilhada A2/B2.
g2 = defaultdict(list)
for r in metricas:
    if r["experimento"] == "B" or r["configuracao"] == "A2_B2":
        g2[num(r["intervalo_chegada_ms"])].append(num(r["espera_p95_ms"]))
x = sorted(g2)
y = [statistics.mean(g2[t]) for t in x]
err = [statistics.stdev(g2[t]) if len(g2[t]) > 1 else 0 for t in x]
plt.figure(figsize=(7, 4.5))
plt.errorbar(x, y, yerr=err, marker="o", capsize=4)
plt.title("G2 — Intervalo entre chegadas × p95 da espera dos atendidos")
plt.xlabel("Intervalo entre chegadas (ms)")
plt.ylabel("p95 da espera (ms)")
plt.grid(True, alpha=.3)
plt.tight_layout()
plt.savefig("g2.png", dpi=160)
plt.close()

# G3: cinco blocos de 40 clientes para a configuração de referência (4 cadeiras, 2 ms).
por_repeticao = defaultdict(list)
for e in eventos:
    if e["configuracao"] != "A2_B2":
        continue
    rep = int(e["repeticao"])
    bloco = (int(e["cliente_id"]) - 1) // 40
    if bloco < 5:
        por_repeticao[rep].append(e)
blocos = {i: {"esperas": [], "aceitos": 0, "rejeitados": 0} for i in range(5)}
for linhas in por_repeticao.values():
    chegada = {}
    inicio = {}
    estados = defaultdict(lambda: {"aceitos": 0, "rejeitados": 0})
    for e in linhas:
        cid = int(e["cliente_id"])
        bloco = (cid - 1) // 40
        t = num(e["tempo_ms"])
        if e["event"] == "CHEGADA_ACEITA":
            chegada[cid] = t
            estados[bloco]["aceitos"] += 1
        elif e["event"] == "CHEGADA_REJEITADA":
            estados[bloco]["rejeitados"] += 1
        elif e["event"] == "INICIO":
            inicio[cid] = t
    for cid, t_inicio in inicio.items():
        bloco = (cid - 1) // 40
        if cid in chegada:
            blocos[bloco]["esperas"].append(t_inicio - chegada[cid])
    for b in range(5):
        blocos[b]["aceitos"] += estados[b]["aceitos"]
        blocos[b]["rejeitados"] += estados[b]["rejeitados"]

reps = max(1, len(por_repeticao))
x = list(range(1, 6))
medias_p95, erros_p95, anotacoes = [], [], []
for b in range(5):
    # p95 individual por repetição, depois média e desvio padrão entre repetições.
    amostras = []
    for linhas in por_repeticao.values():
        chegadas, inicios = {}, {}
        for e in linhas:
            cid = int(e["cliente_id"])
            if (cid - 1) // 40 != b:
                continue
            if e["event"] == "CHEGADA_ACEITA": chegadas[cid] = num(e["tempo_ms"])
            elif e["event"] == "INICIO": inicios[cid] = num(e["tempo_ms"])
        amostras.append(p95([inicios[c] - t for c, t in chegadas.items() if c in inicios]))
    amostras = [v for v in amostras if math.isfinite(v)]
    medias_p95.append(statistics.mean(amostras) if amostras else math.nan)
    erros_p95.append(statistics.stdev(amostras) if len(amostras) > 1 else 0.0)
    anotacoes.append(f"aceitos {blocos[b]['aceitos']/reps:.1f}\nrejeitados {blocos[b]['rejeitados']/reps:.1f}")
plt.figure(figsize=(8, 5))
plt.errorbar(x, medias_p95, yerr=erros_p95, marker="o", capsize=4)
for i, anotacao in enumerate(anotacoes):
    if math.isfinite(medias_p95[i]):
        plt.annotate(anotacao, (x[i], medias_p95[i]), xytext=(0, 9), textcoords="offset points", ha="center", fontsize=8)
plt.title("G3 — Blocos consecutivos de 40 chegadas × p95 da espera")
plt.xlabel("Bloco de chegadas (clientes 1–40, 41–80, …)")
plt.ylabel("p95 da espera dos atendidos (ms)")
plt.xticks(x)
plt.grid(True, alpha=.3)
plt.tight_layout()
plt.savefig("g3.png", dpi=160)
plt.close()

# G4: série temporal de uma repetição de referência; ocupado=1 durante cada corte.
g4 = [e for e in eventos if e["configuracao"] == "A2_B2" and int(e["repeticao"]) == 1]
g4.sort(key=lambda e: num(e["tempo_ms"]))
tx = [0.0] + [num(e["tempo_ms"]) for e in g4]
q = [0] + [int(e["fila"]) for e in g4]
busy = [0] + [int(e["barbeiro_ocupado"]) for e in g4]
fig, ax1 = plt.subplots(figsize=(9, 5))
ax1.step(tx, q, where="post", color="tab:blue", label="Clientes na fila")
ax1.set_xlabel("Tempo da simulação (ms)")
ax1.set_ylabel("Tamanho da fila", color="tab:blue")
ax1.tick_params(axis="y", labelcolor="tab:blue")
ax1.set_ylim(bottom=0)
ax2 = ax1.twinx()
ax2.step(tx, busy, where="post", color="tab:orange", alpha=.8, label="Barbeiro ocupado")
ax2.set_ylabel("Estado do barbeiro (0 ocioso, 1 ocupado)", color="tab:orange")
ax2.set_yticks([0, 1], labels=["Ocioso", "Ocupado"])
ax2.set_ylim(-.1, 1.1)
fig.suptitle("G4 — Tempo × tamanho da fila e estado do barbeiro (referência, repetição 1)")
fig.tight_layout()
fig.savefig("g4.png", dpi=160)
plt.close(fig)

for nome in ("g1.png", "g2.png", "g3.png", "g4.png"):
    print(f"Gerado: {nome}")
