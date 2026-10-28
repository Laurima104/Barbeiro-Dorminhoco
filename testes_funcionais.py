#!/usr/bin/env python3
"""Testes determinísticos dos cenários de fila, encerramento e falhas."""
import csv
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CC = os.environ.get("CC", "gcc")


def executar(binary, intervalo, cadeiras, clientes, diretorio, rotulo, env_extra=None, limite="10"):
    csv_path = diretorio / f"{rotulo}.csv"
    eventos_path = diretorio / f"{rotulo}_eventos.csv"
    env = os.environ.copy()
    env.update({"BARBEIRO_CLIENTES": str(clientes), "TRACE_CSV": str(eventos_path), "SIM_TIMEOUT_S": limite})
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run(
        [str(binary), str(intervalo), str(cadeiras), str(csv_path), "TESTE", rotulo, "1"],
        env=env, text=True, capture_output=True, timeout=float(limite) + 5,
    )
    return proc, csv_path, eventos_path


def ler_csv(path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def verificar_fifo_e_sem_sobreposicao(eventos):
    aceitos = sorted((e for e in eventos if e["event"] == "CHEGADA_ACEITA"), key=lambda e: float(e["tempo_ms"]))
    inicios = sorted((e for e in eventos if e["event"] == "INICIO"), key=lambda e: float(e["tempo_ms"]))
    fins = {e["cliente_id"]: float(e["tempo_ms"]) for e in eventos if e["event"] == "FIM"}
    assert [e["cliente_id"] for e in aceitos] == [e["cliente_id"] for e in inicios], "FIFO violado"
    anterior = -1.0
    for inicio in inicios:
        t_inicio = float(inicio["tempo_ms"])
        t_fim = fins[inicio["cliente_id"]]
        assert t_inicio >= anterior, "início de serviço sobreposto"
        assert t_fim > t_inicio, "serviço sem duração positiva"
        anterior = t_fim


def main():
    with tempfile.TemporaryDirectory(prefix="barbeiro_tests_") as temp:
        work = Path(temp)
        binary = work / "barbeiro_testes"
        subprocess.run([
            CC, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic",
            "-pthread", "-DBARBEIRO_TESTING", "-DTEMPO_CORTE_MS=500.0",
            str(ROOT / "simulacao.c"), "-o", str(binary), "-lm",
        ], check=True, timeout=30)

        # Sala vazia: nenhum cliente, barbeiro acorda no encerramento e termina.
        proc, csv_path, eventos_path = executar(binary, 2, 4, 0, work, "sala_vazia")
        assert proc.returncode == 0, proc.stderr
        row = ler_csv(csv_path)[0]
        assert row["status"] == "OK" and row["clientes_planejados"] == "0"
        assert row["participantes_encerrados"] == "1" and row["amostras_espera"] == "0"
        eventos = ler_csv(eventos_path)
        assert any(e["event"] == "BARBEIRO_ENCERRADO" for e in eventos)

        # Sala cheia/rejeições e encerramento solicitado enquanto há fila pendente.
        proc, csv_path, eventos_path = executar(binary, 0, 1, 200, work, "fila_cheia")
        assert proc.returncode == 0, proc.stderr
        row = ler_csv(csv_path)[0]
        eventos = ler_csv(eventos_path)
        assert row["status"] == "OK" and int(row["clientes_rejeitados"]) > 0
        assert max(int(e["fila"]) for e in eventos if e["fila"] != "N/A") == 1
        fechamento = next(e for e in eventos if e["event"] == "ENCERRAMENTO_SOLICITADO")
        assert int(fechamento["fila"]) == 1, "o teste não encerrou com cliente aguardando"
        assert int(row["clientes_aceitos"]) == int(row["clientes_atendidos"])
        assert int(row["clientes_atendidos"]) + int(row["clientes_rejeitados"]) == 200
        assert int(row["participantes_encerrados"]) == 201
        assert any(e["event"] == "BARBEIRO_ENCERRADO" and e["fila"] == "0" for e in eventos)
        verificar_fifo_e_sem_sobreposicao(eventos)

        # Falha forçada no pthread_create depois de três clientes: abortar barreira,
        # realizar joins e sair, sem deixar threads bloqueadas.
        env = os.environ.copy()
        env.update({"BARBEIRO_CLIENTES": "20", "BARBEIRO_TEST_FALHAR_CRIACAO_APOS": "3"})
        proc = subprocess.run([str(binary), "2", "4"], env=env, text=True,
                              capture_output=True, timeout=10)
        assert proc.returncode != 0 and "pthread_create cliente" in proc.stderr

        # Limite de execução: timeout explícito e registro de falha no CSV bruto.
        timeout_csv, timeout_events = work / "timeout.csv", work / "timeout_eventos.csv"
        env = os.environ.copy()
        env.update({"BARBEIRO_CLIENTES": "200", "SIM_TIMEOUT_S": "0.05"})
        proc = subprocess.run(["timeout", "--signal=TERM", "--kill-after=1s", "0.05s",
                               str(binary), "5", "4"], env=env, text=True,
                              capture_output=True, timeout=5)
        assert proc.returncode in (124, 137), f"timeout não ocorreu: {proc.returncode}"
        subprocess.run([sys.executable, str(ROOT / "registrar_falha.py"),
                        str(timeout_csv), str(timeout_events), "TESTE", "TIMEOUT", "1", "5", "4", "0.05", "TIMEOUT"],
                       check=True, timeout=5)
        row = ler_csv(timeout_csv)[0]
        assert row["status"] == "TIMEOUT" and row["timeout"] == "1"
        assert ler_csv(timeout_events)[0]["event"] == "RUN_TIMEOUT"

    print("Testes funcionais OK: sala vazia, fila cheia/rejeição, FIFO, drenagem no encerramento, falha de criação e timeout.")


if __name__ == "__main__":
    main()
