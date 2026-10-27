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
./executar_experimentos.sh ./barbeiro_dorminhoco resultados_experimentos.csv
python3 gerar_graficos.py resultados_experimentos.csv
```

O script executa cinco repetições para cada uma das cinco configurações distintas, gerando um CSV e quatro PNGs. O desvio padrão entre repetições é mostrado como barra de erro.

## Métricas e interpretação

O tempo de espera é medido da entrada na fila ao início do corte; rejeitados não entram nessa média. O p95 usa a posição `ceil(0,95 n)`. A vazão conta somente cortes concluídos. A utilização soma as durações medidas entre início e fim de cada corte e divide pelo tempo total medido.

Uma taxa de rejeição alta pode reduzir artificialmente a espera média, porque clientes rejeitados não aguardam. Interprete espera junto com rejeição e vazão. O corte solicitado é 2 ms, mas o tempo observado pode ser maior por causa do escalonamento e da concorrência. Intervalos solicitados abaixo de 1 ms também podem variar: `nanosleep()` expressa a duração, mas o sistema operacional não garante que cada thread execute exatamente no instante pedido. Resultados mudam entre repetições.
