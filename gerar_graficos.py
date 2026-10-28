#!/usr/bin/env python3
"""Gera G1–G4 e o resumo do G3 a partir dos CSVs da simulação."""
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
    ordenados = sorted(valores)
    if not ordenados:
        return math.nan
    return ordenados[math.ceil(0.95 * len(ordenados)) - 1]


def barras_min_max(valores):
    medias = [statistics.mean(v) for v in valores]
    inferior = [m - min(v) for m, v in zip(medias, valores)]
    superior = [max(v) - m for m, v in zip(medias, valores)]
    return medias, [inferior, superior]


if len(sys.argv) not in (2, 3):
    raise SystemExit(f"Uso: {sys.argv[0]} resultados_experimentos.csv [eventos_experimentos.csv]")
metricas = carregar(sys.argv[1])
eventos = carregar(sys.argv[2]) if len(sys.argv) == 3 else carregar("eventos_experimentos.csv")

contagem = defaultdict(int)
for r in metricas:
    contagem[r["configuracao"]] += 1
if len(metricas) != 45 or len(contagem) != 9 or any(n != 5 for n in contagem.values()):
    raise SystemExit("Esperados 45 resultados: nove combinações com cinco repetições cada.")

# G1: vazão média e mínimo–máximo das cinco repetições; intervalo fixo em 2 ms.
g1 = defaultdict(list)
for r in metricas:
    if r.get("status") == "OK" and math.isclose(num(r["intervalo_chegada_ms"]), 2.0):
        g1[int(r["cadeiras"])].append(num(r["vazao_clientes_s"]))
x = sorted(g1)
y, yerr = barras_min_max([g1[c] for c in x])
plt.figure(figsize=(7, 4.5))
plt.errorbar(x, y, yerr=yerr, marker="o", capsize=4, label="média; barras: mínimo–máximo")
for cadeira, media in zip(x, y):
    plt.annotate(f"n={len(g1[cadeira])}/5", (cadeira, media), xytext=(0, 9),
                 textcoords="offset points", ha="center", fontsize=8)
plt.title("G1 — Cadeiras × clientes atendidos/s (intervalo fixo de 2 ms)")
plt.xlabel("Cadeiras de espera")
plt.ylabel("Clientes atendidos por segundo")
plt.grid(True, alpha=.3)
plt.legend()
plt.tight_layout()
plt.savefig("g1.png", dpi=160)
plt.close()

# G2: p95 de cada execução, depois média e mínimo–máximo; cadeiras fixas em 4.
g2 = defaultdict(list)
for r in metricas:
    if r.get("status") == "OK" and int(r["cadeiras"]) == 4:
        g2[num(r["intervalo_chegada_ms"])].append(num(r["espera_p95_ms"]))
x = sorted(g2)
y, yerr = barras_min_max([g2[t] for t in x])
plt.figure(figsize=(7, 4.5))
plt.errorbar(x, y, yerr=yerr, marker="o", capsize=4, label="média; barras: mínimo–máximo")
for intervalo, media in zip(x, y):
    plt.annotate(f"n={len(g2[intervalo])}/5", (intervalo, media), xytext=(0, 9),
                 textcoords="offset points", ha="center", fontsize=8)
plt.title("G2 — Intervalo × p95 da espera dos atendidos (4 cadeiras)")
plt.xlabel("Intervalo entre chegadas (ms)")
plt.ylabel("p95 da espera (ms)")
plt.grid(True, alpha=.3)
plt.legend()
plt.tight_layout()
plt.savefig("g2.png", dpi=160)
plt.close()

# G3: classifica as 200 chegadas pela ordem temporal efetiva de cada execução.
eventos_ref = defaultdict(list)
repeticoes_validas_g3 = {
    int(r["repeticao"]) for r in metricas
    if r["configuracao"] == "C4_I2ms" and r.get("status") == "OK"
}
for e in eventos:
    if e["configuracao"] == "C4_I2ms" and int(e["repeticao"]) in repeticoes_validas_g3:
        eventos_ref[int(e["repeticao"])].append(e)
if not eventos_ref:
    raise SystemExit("G3 não tem execuções válidas de C4_I2ms.")

p95_por_bloco = [[] for _ in range(5)]
aceitos_por_bloco = [[] for _ in range(5)]
rejeitados_por_bloco = [[] for _ in range(5)]
for repeticao, linhas in eventos_ref.items():
    chegadas = sorted(
        (e for e in linhas if e["event"] == "CHEGADA"),
        key=lambda e: num(e["tempo_ms"]),
    )
    if len(chegadas) != 200:
        raise SystemExit(f"G3: repetição {repeticao} tem {len(chegadas)} chegadas, esperado 200.")
    inicio = {int(e["cliente_id"]): num(e["tempo_ms"])
              for e in linhas if e["event"] == "INICIO"}
    aceitos_por_id = {int(e["cliente_id"]): num(e["tempo_ms"])
                      for e in linhas if e["event"] == "CHEGADA_ACEITA"}
    rejeitados_por_id = {int(e["cliente_id"]) for e in linhas
                         if e["event"] == "CHEGADA_REJEITADA"}
    for bloco in range(5):
        grupo = chegadas[bloco * 40:(bloco + 1) * 40]
        ids_aceitos = [int(e["cliente_id"]) for e in grupo
                       if int(e["cliente_id"]) in aceitos_por_id]
        ids_rejeitados = [int(e["cliente_id"]) for e in grupo
                          if int(e["cliente_id"]) in rejeitados_por_id]
        esperas = [inicio[cid] - aceitos_por_id[cid]
                   for cid in ids_aceitos if cid in inicio]
        aceitos, rejeitados = len(ids_aceitos), len(ids_rejeitados)
        if len(grupo) != 40 or len(esperas) != aceitos or aceitos + rejeitados != 40:
            raise SystemExit(f"G3: contagem ou amostras inválidas no bloco {bloco + 1}.")
        p95_por_bloco[bloco].append(p95(esperas))
        aceitos_por_bloco[bloco].append(aceitos)
        rejeitados_por_bloco[bloco].append(rejeitados)

resumo_g3 = []
for b in range(5):
    resumo_g3.append({
        "bloco": b + 1,
        "posicao_chegada_inicial": b * 40 + 1,
        "posicao_chegada_final": (b + 1) * 40,
        "repeticoes_validas": len(p95_por_bloco[b]),
        "amostras_espera_media": statistics.mean(aceitos_por_bloco[b]),
        "aceitos_media": statistics.mean(aceitos_por_bloco[b]),
        "rejeitados_media": statistics.mean(rejeitados_por_bloco[b]),
        "p95_espera_media_ms": statistics.mean(p95_por_bloco[b]),
        "p95_espera_min_ms": min(p95_por_bloco[b]),
        "p95_espera_max_ms": max(p95_por_bloco[b]),
    })
with open("g3_resumo.csv", "w", newline="", encoding="utf-8") as arquivo:
    campos = list(resumo_g3[0])
    escritor = csv.DictWriter(arquivo, fieldnames=campos)
    escritor.writeheader()
    escritor.writerows(resumo_g3)

x = [r["bloco"] for r in resumo_g3]
y = [r["p95_espera_media_ms"] for r in resumo_g3]
yerr = [
    [r["p95_espera_media_ms"] - r["p95_espera_min_ms"] for r in resumo_g3],
    [r["p95_espera_max_ms"] - r["p95_espera_media_ms"] for r in resumo_g3],
]
plt.figure(figsize=(8.5, 5.2))
plt.errorbar(x, y, yerr=yerr, marker="o", capsize=4, label="média; barras: mínimo–máximo")
for i, r in enumerate(resumo_g3):
    anotacao = f"aceitos méd. {r['aceitos_media']:.1f}\nrejeitados méd. {r['rejeitados_media']:.1f}\nn≈{r['amostras_espera_media']:.1f}"
    if i == 0:
        deslocamento, alinhamento = (12, 9), "left"
    elif i == 4:
        deslocamento, alinhamento = (-12, 9), "right"
    else:
        deslocamento, alinhamento = (0, 9), "center"
    plt.annotate(anotacao, (x[i], y[i]), xytext=deslocamento,
                 textcoords="offset points", ha=alinhamento, fontsize=8)
plt.title(f"G3 — Blocos temporais de 40 chegadas × p95 (4 cadeiras, 2 ms; n={len(eventos_ref)} válidas)")
plt.xlabel("Bloco pela ordem efetiva das chegadas (1–40, 41–80, …)")
plt.ylabel("p95 da espera dos atendidos (ms)")
plt.xticks(x)
plt.grid(True, alpha=.3)
plt.legend()
plt.tight_layout()
plt.savefig("g3.png", dpi=160)
plt.close()

# G4: execução de referência sob disputa; fila em degraus e serviço em faixas.
repeticoes_validas_g4 = [int(r["repeticao"]) for r in metricas
                         if r["configuracao"] == "C4_I2ms" and r.get("status") == "OK"]
if not repeticoes_validas_g4:
    raise SystemExit("G4 não tem execução válida da configuração C4_I2ms.")
repeticao_g4 = min(repeticoes_validas_g4)
g4 = [e for e in eventos if e["configuracao"] == "C4_I2ms" and int(e["repeticao"]) == repeticao_g4
      and e["event"] not in ("CHEGADA", "CHEGADA_REJEITADA")]
if not g4:
    raise SystemExit("G4 precisa dos eventos da repetição 1 da configuração C4_I2ms.")
g4.sort(key=lambda e: num(e["tempo_ms"]))
tx = [0.0] + [num(e["tempo_ms"]) for e in g4]
fila = [0] + [int(e["fila"]) for e in g4]
fim_simulacao = max(tx)
ocupado, t_inicio = [], None
for e in g4:
    t = num(e["tempo_ms"])
    if e["event"] == "INICIO":
        t_inicio = t
    elif e["event"] == "FIM" and t_inicio is not None:
        ocupado.append((t_inicio, max(0.0, t - t_inicio)))
        t_inicio = None
ocioso, cursor = [], 0.0
for inicio, duracao in ocupado:
    if inicio > cursor:
        ocioso.append((cursor, inicio - cursor))
    cursor = max(cursor, inicio + duracao)
if fim_simulacao > cursor:
    ocioso.append((cursor, fim_simulacao - cursor))

fig, (ax_fila, ax_estado) = plt.subplots(2, 1, figsize=(10, 6), sharex=True,
                                         gridspec_kw={"height_ratios": [3, 1]})
ax_fila.step(tx, fila, where="post", color="tab:blue")
ax_fila.set_ylabel("Clientes na fila")
ax_fila.set_ylim(bottom=0)
ax_fila.set_title("Tamanho da fila ao longo do tempo")
ax_fila.grid(True, alpha=.3)
ax_estado.broken_barh(ocioso, (0.15, 0.7), facecolors="lightgray")
ax_estado.broken_barh(ocupado, (0.15, 0.7), facecolors="tab:orange")
ax_estado.set_yticks([])
ax_estado.set_xlim(0, fim_simulacao)
ax_estado.set_xlabel("Tempo da simulação (ms)")
ax_estado.set_title("Estado do barbeiro")
ax_estado.legend(handles=[Patch(color="tab:orange", label="Ocupado / atendendo"),
                          Patch(color="lightgray", label="Ocioso")], loc="upper right", ncols=2)
fig.suptitle(f"G4 — Fila e estado do barbeiro (4 cadeiras, 2 ms, repetição {repeticao_g4})")
fig.tight_layout()
fig.savefig("g4.png", dpi=160)
plt.close(fig)

for nome in ("g1.png", "g2.png", "g3.png", "g4.png", "g3_resumo.csv"):
    print(f"Gerado: {nome}")
