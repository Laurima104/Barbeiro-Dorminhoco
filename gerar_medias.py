#!/usr/bin/env python3
"""Calcula médias por configuração sem ocultar repetições inválidas."""
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

colunas_media = [campo for campo in campos if campo not in chaves and campo != "repeticao"]
campos_saida = chaves + ["repeticoes", "execucoes_validas"] + colunas_media
with open(saida, "w", newline="", encoding="utf-8") as arquivo:
    escritor = csv.DictWriter(arquivo, fieldnames=campos_saida)
    escritor.writeheader()
    for chave in sorted(grupos):
        linhas = grupos[chave]
        validas = [linha for linha in linhas if linha.get("status") == "OK"]
        resultado = {
            "experimento": chave[0],
            "configuracao": chave[1],
            "repeticoes": len(linhas),
            "execucoes_validas": len(validas),
        }
        for campo in colunas_media:
            if campo == "status":
                resultado[campo] = "OK" if len(validas) == len(linhas) else "COM_FALHAS"
                continue
            base = linhas if campo == "timeout" else validas
            valores_textuais = {linha.get(campo, "") for linha in base}
            amostras = []
            for linha in base:
                try:
                    numero = float(linha.get(campo, ""))
                except (TypeError, ValueError):
                    continue
                if math.isfinite(numero):
                    amostras.append(numero)
            if amostras:
                resultado[campo] = f"{statistics.mean(amostras):.9f}"
            elif len(valores_textuais) == 1:
                resultado[campo] = next(iter(valores_textuais)) or "N/A"
            else:
                resultado[campo] = "N/A"
        escritor.writerow(resultado)
print(f"Médias de {len(grupos)} combinações gravadas em {saida}")
