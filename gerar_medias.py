#!/usr/bin/env python3
"""Calcula a média das repetições agrupadas por configuração no CSV bruto."""
import csv
import math
import statistics
import sys
from collections import defaultdict


if len(sys.argv) != 3:
    raise SystemExit(f"Uso: {sys.argv[0]} resultados_experimentos.csv media_resultados_experimentos.csv")

entrada, saida = sys.argv[1:]
with open(entrada, newline="", encoding="utf-8") as arquivo:
    leitor = csv.DictReader(arquivo)
    campos = leitor.fieldnames
    dados = list(leitor)
if not campos:
    raise SystemExit("CSV de entrada sem cabeçalho")

chaves = ["experimento", "configuracao"]
grupos = defaultdict(list)
for linha in dados:
    grupos[tuple(linha[k] for k in chaves)].append(linha)
if len(grupos) != 9 or any(len(linhas) != 5 for linhas in grupos.values()):
    raise SystemExit("Esperadas nove combinações, cada uma com exatamente cinco repetições.")

colunas_media = [
    campo for campo in campos
    if campo not in chaves and campo != "repeticao"
]
with open(saida, "w", newline="", encoding="utf-8") as arquivo:
    campos_saida = chaves + ["repeticoes"] + colunas_media
    escritor = csv.DictWriter(arquivo, fieldnames=campos_saida)
    escritor.writeheader()
    for chave in sorted(grupos):
        linhas = grupos[chave]
        resultado = {"experimento": chave[0], "configuracao": chave[1], "repeticoes": len(linhas)}
        for campo in colunas_media:
            amostras = []
            for linha in linhas:
                valor = linha[campo]
                if valor not in ("", "N/A"):
                    numero = float(valor)
                    if math.isfinite(numero):
                        amostras.append(numero)
            resultado[campo] = f"{statistics.mean(amostras):.9f}" if amostras else "N/A"
        escritor.writerow(resultado)
print(f"Médias de {len(grupos)} combinações gravadas em {saida}")
