# Barbeiro Dorminhoco

## Compilar e executar

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -pthread simulacao.c -o barbeiro_dorminhoco -lm
./barbeiro_dorminhoco
./barbeiro_dorminhoco 0.5 4
./barbeiro_dorminhoco 2 1
```

Sem argumentos, o programa pergunta o intervalo entre chegadas e a quantidade de cadeiras. Também aceita `./barbeiro_dorminhoco <intervalo_ms> <cadeiras_espera>`, útil para automação. Há 200 clientes e cada corte tem duração configurada de 2 ms. O terceiro argumento opcional grava/continua um CSV; argumentos seguintes identificam experimento, configuração e repetição.

## Matriz experimental

```sh
./executar_experimentos.sh ./barbeiro_dorminhoco resultados_experimentos.csv eventos_experimentos.csv media_resultados_experimentos.csv
~/.venvs/matplotlib/bin/python gerar_graficos.py resultados_experimentos.csv eventos_experimentos.csv
~/.venvs/matplotlib/bin/python testes_funcionais.py
```

Neste ambiente, Python 3.13 e matplotlib estão instalados no ambiente local `~/.venvs/matplotlib` (sem alterar os pacotes do sistema).

O script executa cinco repetições para cada combinação entre 1, 4 e 8 cadeiras e intervalos de 0,5, 2 e 5 ms (45 simulações), com limite de 10 s por execução (`TIMEOUT_SECONDS` pode alterá-lo). Exige GNU `timeout`. Registra status, violações, participantes encerrados, timeout e limite, além de marcar semente como `N/A` e aleatoriedade como `nao`. Uma execução interrompida recebe uma linha explícita `TIMEOUT` ou `ERRO`. Produz CSV bruto, CSV de eventos e médias por configuração; a coluna `execucoes_validas` e o status deixam falhas visíveis.

Para testar especificamente sem clientes, fila cheia/rejeição, FIFO, corte sem sobreposição, encerramento com fila pendente, falha na criação de thread e timeout, execute `testes_funcionais.py`. O script compila uma variante de teste com serviço mais longo para tornar a disputa reproduzível; a simulação experimental continua usando 2 ms.

Os gráficos gerados são: G1, cadeiras versus vazão no intervalo de 2 ms; G2, intervalo versus p95 mantendo quatro cadeiras; G3, p95 por cinco blocos temporais de 40 chegadas efetivas na referência (4 cadeiras, 2 ms), com médias de aceitos/rejeitados e quantidade de amostras por bloco; G4, tamanho da fila e estado ocupado/ocioso ao longo do tempo na primeira repetição da referência. G1–G3 mostram a média das cinco repetições e barras de mínimo a máximo; G4 mostra uma repetição individual. G3 também gera `g3_resumo.csv`.

Ambiente desta coleta: Debian 13 no WSL2, kernel Linux 6.18.33.2, arquitetura x86_64, 16 CPUs lógicas e GCC 14.2.0. Compilação usada: comando acima. Os integrantes do grupo ainda precisam ser identificados no README para a entrega final.

## Métricas e interpretação

O tempo de espera é medido da entrada na fila ao início do corte; rejeitados não entram nessa média. O p95 usa a posição `ceil(0,95 n)`. A vazão conta somente cortes concluídos. A utilização soma as durações medidas entre início e fim de cada corte e divide pelo tempo total medido.

Uma taxa de rejeição alta pode reduzir artificialmente a espera média, porque clientes rejeitados não aguardam. Interprete espera junto com rejeição e vazão. O corte solicitado é 2 ms, mas o tempo observado pode ser maior por causa do escalonamento e da concorrência. Intervalos solicitados abaixo de 1 ms também podem variar: `nanosleep()` expressa a duração, mas o sistema operacional não garante que cada thread execute exatamente no instante pedido. Resultados mudam entre repetições.
