#!/usr/bin/env python3
"""Gera G1–G4 a partir dos CSVs de métricas e eventos da simulação."""
import csv
import math
import statistics
import sys
from collections import defaultdict

import matplotlib.pyplot as plt
from matplotlib.patches import Patch


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
contagem = defaultdict(int)
for r in metricas:
    contagem[r["configuracao"]] += 1
if len(metricas) != 45 or len(contagem) != 9 or any(n != 5 for n in contagem.values()):
    raise SystemExit("Esperados 45 resultados: nove combinações com cinco repetições cada.")

# G1: vazão por número de cadeiras, usando somente as execuções de 2 ms.
g1 = defaultdict(list)
for r in metricas:
    if math.isclose(num(r["intervalo_chegada_ms"]), 2.0):
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

# G2: p95 de espera por intervalo, mantendo quatro cadeiras.
g2 = defaultdict(list)
for r in metricas:
    if int(r["cadeiras"]) == 4:
        g2[num(r["intervalo_chegada_ms"])].append(num(r["espera_p95_ms"]))
x = sorted(g2)
y = [statistics.mean(g2[t]) for t in x]
err = [statistics.stdev(g2[t]) if len(g2[t]) > 1 else 0 for t in x]
plt.figure(figsize=(7, 4.5))
plt.errorbar(x, y, yerr=err, marker="o", capsize=4)
plt.title("G2 — Intervalo × p95 da espera dos atendidos (4 cadeiras)")
plt.xlabel("Intervalo entre chegadas (ms)")
plt.ylabel("p95 da espera (ms)")
plt.grid(True, alpha=.3)
plt.tight_layout()
plt.savefig("g2.png", dpi=160)
plt.close()

# G3: cinco blocos de 40 clientes para a referência (4 cadeiras, 2 ms).
por_repeticao = defaultdict(list)
for e in eventos:
    if e["configuracao"] != "C4_I2ms":
        continue
    rep = int(e["repeticao"])
    bloco = (int(e["cliente_id"]) - 1) // 40
    if bloco < 5:
        por_repeticao[rep].append(e)
if len(por_repeticao) != 5:
    raise SystemExit("G3 precisa dos eventos das cinco repetições C4_I2ms.")
blocos = {i: {"aceitos": [], "rejeitados": []} for i in range(5)}

x = list(range(1, 6))
medias_p95, erros_p95, anotacoes = [], [], []
for b in range(5):
    # p95 individual por repetição, depois média e desvio padrão entre repetições.
    amostras = []
    for linhas in por_repeticao.values():
        chegadas, inicios = {}, {}
        aceitos, rejeitados = 0, 0
        for e in linhas:
            cid = int(e["cliente_id"])
            if (cid - 1) // 40 != b:
                continue
            if e["event"] == "CHEGADA_ACEITA":
                chegadas[cid] = num(e["tempo_ms"])
                aceitos += 1
            elif e["event"] == "CHEGADA_REJEITADA":
                rejeitados += 1
            elif e["event"] == "INICIO": inicios[cid] = num(e["tempo_ms"])
        amostras.append(p95([inicios[c] - t for c, t in chegadas.items() if c in inicios]))
        blocos[b]["aceitos"].append(aceitos)
    blocos[b]["rejeitados"].append(rejeitados)
    if aceitos + rejeitados != 40:
        raise SystemExit(f"Bloco {b + 1} da repetição não contabiliza 40 chegadas.")
    amostras = [v for v in amostras if math.isfinite(v)]
    medias_p95.append(statistics.mean(amostras) if amostras else math.nan)
    erros_p95.append(statistics.stdev(amostras) if len(amostras) > 1 else 0.0)
    anotacoes.append(f"aceitos méd. {statistics.mean(blocos[b]['aceitos']):.1f}\nrejeitados méd. {statistics.mean(blocos[b]['rejeitados']):.1f}")
plt.figure(figsize=(8, 5))
plt.errorbar(x, medias_p95, yerr=erros_p95, marker="o", capsize=4)
for i, anotacao in enumerate(anotacoes):
    if math.isfinite(medias_p95[i]):
        if i == 0:
            deslocamento, alinhamento = (12, 9), "left"
        elif i == 4:
            deslocamento, alinhamento = (-12, 9), "right"
        else:
            deslocamento, alinhamento = (0, 9), "center"
        plt.annotate(anotacao, (x[i], medias_p95[i]), xytext=deslocamento,
                     textcoords="offset points", ha=alinhamento, fontsize=8)
plt.title("G3 — Blocos de 40 chegadas × p95 (média de cinco repetições)")
plt.xlabel("Bloco de chegadas (clientes 1–40, 41–80, …)")
plt.ylabel("p95 da espera dos atendidos (ms)")
plt.xticks(x)
plt.grid(True, alpha=.3)
plt.tight_layout()
plt.savefig("g3.png", dpi=160)
plt.close()

# G4: série temporal de uma repetição de referência; os intervalos vêm de INICIO/FIM.
g4 = [e for e in eventos if e["configuracao"] == "C4_I2ms" and int(e["repeticao"]) == 1]
if not g4:
    raise SystemExit("G4 precisa dos eventos da repetição 1 da configuração C4_I2ms.")
g4.sort(key=lambda e: num(e["tempo_ms"]))
tempos = [num(e["tempo_ms"]) for e in g4]
fim_simulacao = max(tempos)
tx = [0.0] + tempos
q = [0] + [int(e["fila"]) for e in g4]
intervalos_ocupado, inicio_corte = [], None
for e in g4:
    t = num(e["tempo_ms"])
    if e["event"] == "INICIO":
        inicio_corte = t
    elif e["event"] == "FIM" and inicio_corte is not None:
        intervalos_ocupado.append((inicio_corte, max(0.0, t - inicio_corte)))
        inicio_corte = None
intervalos_ocioso = []
cursor = 0.0
for inicio, duracao in intervalos_ocupado:
    if inicio > cursor:
        intervalos_ocioso.append((cursor, inicio - cursor))
    cursor = max(cursor, inicio + duracao)
if fim_simulacao > cursor:
    intervalos_ocioso.append((cursor, fim_simulacao - cursor))

fig, (ax_fila, ax_estado) = plt.subplots(2, 1, figsize=(10, 6), sharex=True,
                                         gridspec_kw={"height_ratios": [3, 1]})
ax_fila.step(tx, q, where="post", color="tab:blue")
ax_fila.set_ylabel("Clientes na fila")
ax_fila.set_ylim(bottom=0)
ax_fila.set_title("Tamanho da fila ao longo do tempo")
ax_fila.grid(True, alpha=.3)
ax_estado.broken_barh(intervalos_ocioso, (0.15, 0.7), facecolors="lightgray")
ax_estado.broken_barh(intervalos_ocupado, (0.15, 0.7), facecolors="tab:orange")
ax_estado.set_yticks([])
ax_estado.set_xlim(0, fim_simulacao)
ax_estado.set_xlabel("Tempo da simulação (ms)")
ax_estado.set_title("Estado do barbeiro")
ax_estado.legend(handles=[Patch(color="tab:orange", label="Ocupado / atendendo"),
                          Patch(color="lightgray", label="Ocioso")], loc="upper right", ncols=2)
fig.suptitle("G4 — Fila e estado do barbeiro (4 cadeiras, 2 ms, repetição 1)")
fig.tight_layout()
fig.savefig("g4.png", dpi=160)
plt.close(fig)

for nome in ("g1.png", "g2.png", "g3.png", "g4.png"):
    print(f"Gerado: {nome}")
