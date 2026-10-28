#!/usr/bin/env python3
"""Registra timeout ou falha que impediu o binário de escrever seu CSV."""
import csv
import sys
from pathlib import Path


CAMPOS = [
    "experimento", "configuracao", "repeticao", "semente", "aleatoriedade",
    "clientes_planejados", "clientes_aceitos", "clientes_atendidos", "clientes_rejeitados",
    "cadeiras", "intervalo_chegada_ms", "atendimento_configurado_ms", "tempo_total_ms",
    "vazao_clientes_s", "amostras_espera", "espera_media_ms", "espera_p95_ms",
    "espera_maxima_ms", "taxa_rejeicao_pct", "utilizacao_pct", "participantes_encerrados",
    "violacoes", "status", "timeout", "limite_timeout_s",
]


if len(sys.argv) != 10:
    raise SystemExit("Uso: registrar_falha.py csv eventos.csv experimento configuracao repeticao intervalo cadeiras timeout_s TIMEOUT|ERRO")
path, path_eventos, experimento, configuracao, repeticao, intervalo, cadeiras, timeout_s, tipo = sys.argv[1:]
if tipo not in ("TIMEOUT", "ERRO"):
    raise SystemExit("Tipo de falha deve ser TIMEOUT ou ERRO")
arquivo = Path(path)
linhas = []
tem_cabecalho = arquivo.exists() and arquivo.stat().st_size > 0
if tem_cabecalho:
    with arquivo.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        linhas = list(reader)
        if reader.fieldnames != CAMPOS:
            raise SystemExit("Cabeçalho CSV incompatível ao registrar falha")
    if any(r["experimento"] == experimento and r["configuracao"] == configuracao
           and r["repeticao"] == repeticao for r in linhas):
        raise SystemExit(0)

row = {campo: "N/A" for campo in CAMPOS}
row.update({
    "experimento": experimento,
    "configuracao": configuracao,
    "repeticao": repeticao,
    "semente": "N/A",
    "aleatoriedade": "nao",
    "clientes_planejados": "200",
    "cadeiras": cadeiras,
    "intervalo_chegada_ms": intervalo,
    "atendimento_configurado_ms": "2",
    "participantes_encerrados": "0",
    "violacoes": "N/A",
    "status": tipo,
    "timeout": "1" if tipo == "TIMEOUT" else "0",
    "limite_timeout_s": timeout_s,
})
with arquivo.open("a", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=CAMPOS)
    if not tem_cabecalho:
        writer.writeheader()
    writer.writerow(row)

if tipo == "TIMEOUT":
    caminho_eventos = Path(path_eventos)
    campos_evento = ["experimento", "configuracao", "repeticao", "tempo_ms", "cliente_id", "event", "fila", "barbeiro_ocupado"]
    tem_cabecalho_eventos = caminho_eventos.exists() and caminho_eventos.stat().st_size > 0
    with caminho_eventos.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=campos_evento)
        if not tem_cabecalho_eventos:
            writer.writeheader()
        writer.writerow({
            "experimento": experimento, "configuracao": configuracao,
            "repeticao": repeticao, "tempo_ms": float(timeout_s) * 1000,
            "cliente_id": 0, "event": "RUN_TIMEOUT", "fila": "N/A",
            "barbeiro_ocupado": "N/A",
        })
