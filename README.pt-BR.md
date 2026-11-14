> **Nota:** [English Version](README.md)

# Kheiron v2.0.0 — CSPRNG Single-Header de Alta Performance em C com Vetorização AVX2 e Zero Dependências

[![CI](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml/badge.svg)](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Standards: C99 / C11](https://img.shields.io/badge/Standards-C99%20%2F%20C11-brightgreen.svg)](#)
[![Zero Dependency](https://img.shields.io/badge/Dependencies-Zero-orange.svg)](#)

Gerador de Números Pseudo-Aleatórios Criptograficamente Seguro (CSPRNG) em arquivo único (*Single-Header*) em C99/C11, construído sobre a cifra de fluxo ChaCha20 (20 rounds, RFC 8439) com motor vetorial **AVX2 de 8 blocos paralelos (~2,5 GB/s)**, despacho dinâmico via CPUID em runtime e fallback automático transparente em C99 puro para qualquer arquitetura (ARM, RISC-V, x86 legado) sem exigir flags globais de compilação.

---

## Proposta de Valor e Portabilidade

- **Single-Header Amalgamado**: Integração imediata com `#define KHEIRON_IMPLEMENTATION` ou modo de linkagem estática com `#define KHEIRON_STATIC`, com zero dependências externas.
- **Despacho Dinâmico CPUID + AVX2 (8 Blocos)**: Detecção em tempo de execução que ativa o kernel vetorial AVX2 de 8 blocos paralelos (512 bytes/iteração) em CPUs x86_64 compatíveis, com fallback automático e transparente para C99 escalar puro em qualquer outra arquitetura.
- **Isolamento Concorrente Lock-Free**: Estado thread-local nativo (`KHEIRON_THREAD_LOCAL`) e instâncias explícitas (`kheiron_ctx_t`) para eliminar contenção entre threads.
- **Forward Secrecy Contínuo**: Auto-rotação compulsória de chave de cifragem a cada 1 MB de dados gerados sem impacto perceptível de performance.
- **Amostragem Sem Modulo Bias**: Algoritmo de Daniel Lemire com multiplicação de 128 bits (`__uint128_t`) e ponto flutuante IEEE 754 (`double` 53 bits / `float` 24 bits) em `[0.0, 1.0)`.
- **Resiliência a Fork**: Invalidação de estado e reseed automático garantido pós-fork via `pthread_atfork` em sistemas POSIX, sem chamadas `madvise` inseguras sobre buffers arbitrários de pilha ou heap.
- **Auto-Teste BIST Embutido**: Verificação automática de integridade do vetor oficial da RFC 8439 e sanitização de memória antes de liberar qualquer byte.
- **Aleatoriedade Empírica**: Verificado com PractRand v0.94 até 1 Terabyte (`2^40` bytes) com zero anomalias estatísticas em todos os checkpoints de teste. Veja [docs/statistical_validation.md](docs/statistical_validation.md).

---

## Guia de Integração e Quickstart

### Inclusão Básica

Em exatamente um arquivo de implementação `.c`:

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
```

Nos demais arquivos do seu projeto:

```c
#include "kheiron.h"
```

Caso deseje embutir todas as rotinas como `static inline` sem expor símbolos globais (ideal para múltiplos módulos sem colisão):

```c
#define KHEIRON_STATIC
#include "kheiron.h"
```

### Exemplo Rápido

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>

int main(void) {
    uint8_t buffer[32];
    kheiron_fill(buffer, sizeof(buffer));

    char password[25];
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    kheiron_string(password, 24, charset);
    printf("Senha: %s\n", password);

    uint64_t dice = kheiron_range_u64(1, 6);
    printf("Dado (1 a 6): %llu\n", (unsigned long long)dice);

    double prob = kheiron_uniform_double();
    printf("Probabilidade [0.0, 1.0): %.8f\n", prob);

    return 0;
}
```

---

## Referência Completa da API

### Inicialização e Contextos Explícitos

| Assinatura | Descrição | Retorno |
|:---|:---|:---:|
| `int kheiron_selftest(void)` | Executa BIST (vetor RFC 8439 e validação de `kheiron_secure_wipe`). | `0` em sucesso; `-1` falha RFC; `-2` falha wipe |
| `int kheiron_ctx_init(kheiron_ctx_t *ctx, const void *seed, size_t seed_len)` | Inicializa uma instância independente de contexto com semente ou entropia de jitter/SO. | `0` em sucesso; `-1` erro de parâmetro |
| `int kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len)` | Gera bytes criptográficos utilizando a instância de contexto explicitada. | `0` em sucesso; `-1` erro |
| `void kheiron_ctx_wipe(kheiron_ctx_t *ctx)` | Zera seguramente a memória do contexto fornecido via barreira de memória. | `void` |
| `int kheiron_derive(kheiron_ctx_t *ctx, void *out, size_t out_len, const void *info, size_t info_len)` | Deriva uma sub-chave com separação de domínio sem comprometer o contexto raiz. | `0` em sucesso; `-1` erro |

### Geração Global e Utilitários de Alto Nível

| Assinatura | Descrição | Retorno |
|:---|:---|:---:|
| `int kheiron_fill(void *buf, size_t len)` | Preenche o buffer com bytes pseudo-aleatórios usando o estado thread-local. | `0` em sucesso; `-1` erro |
| `int kheiron_string(char *dest, size_t len, const char *charset)` | Gera string terminada em `\0` sem modulo bias utilizando máscara de bits e batching. | `0` em sucesso; `-1` erro |
| `uint64_t kheiron_range_u64(uint64_t min, uint64_t max)` | Amostra inteiros no intervalo inclusivo `[min, max]` usando multiplicação de 128 bits (Lemire). | Valor amostrado |
| `double kheiron_uniform_double(void)` | Gera ponto flutuante em `[0.0, 1.0)` com 53 bits de mantissa IEEE 754. | Valor `double` |
| `float kheiron_uniform_float(void)` | Gera ponto flutuante em `[0.0f, 1.0f)` com 24 bits de mantissa IEEE 754. | Valor `float` |
| `int kheiron_shuffle(void *base, size_t nmemb, size_t size)` | Embaralha elementos em memória via algoritmo Fisher-Yates de tempo linear. | `0` em sucesso; `-1` erro |
| `double kheiron_gaussian(double mean, double stddev)` | Gera amostragem de distribuição normal através da transformada polar de Marsaglia. | Valor `double` |
| `int kheiron_reseed(const void *user_entropy, size_t len)` | Injeta nova entropia de usuário misturada com fontes nativas do SO. | `0` em sucesso; `-1` erro |

### Primitivas de Segurança e Baixo Nível

| Assinatura | Descrição | Retorno |
|:---|:---|:---:|
| `void kheiron_secure_wipe(void *v, size_t n)` | Zera buffer com ponteiro volátil e barreira do compilador contra otimização. | `void` |
| `int kheiron_os_entropy(void *buf, size_t len)` | Obtém entropia do sistema operacional (`getrandom`, `/dev/urandom`, `arc4random_buf`, `BCryptGenRandom`). | `0` em sucesso |
| `int kheiron_consttime_cmp(const void *a, const void *b, size_t len)` | Comparação de buffers em tempo constante resistente a ataques de temporização (*timing attacks*). | `0` se iguais; `!= 0` se diferentes |
| `uint32_t kheiron_consttime_select_u32(uint32_t mask, uint32_t a, uint32_t b)` | Seleção condicional em tempo constante (`mask ? a : b`) sem desvios condicionais. | Resultado |
| `uint64_t kheiron_rdtsc(void)` | Leitura direta do contador de ciclos de hardware (`rdtsc` em x86-64, `cntvct_el0` em ARM64). | Ciclos de clock |
| `uint64_t kheiron_jitter_sample(void)` | Coleta de jitter de temporização de execução com barreiras de memória. | Amostra de 64 bits |

---

## Arquitetura e Engenharia de Segurança

### 1. Forward Secrecy & Backtracking Resistance
O estado global ou contextual armazena a chave e nonce do ChaCha20. A cada 1 MB (1.048.576 bytes) produzidos, o gerador entra no ciclo de auto-rotação (`kheiron_rotate_key`), gerando um novo bloco de fluxo, misturando-o ao estado e limpando o bloco intermediário. Mesmo que o estado seja exposto futuramente, dados anteriores não podem ser reconstruídos.

### 2. Sanitização Segura de Memória
A rotina `kheiron_secure_wipe` implementa escrita byte-a-byte através de ponteiro `volatile uint8_t *` sucedida por uma barreira explícita de memória de compilador `__asm__ __volatile__("" : : "r"(p) : "memory")`. Isso impede que o compilador elimine escritas em variáveis sensíveis antes de retornos de função sob otimizações `-O2` e `-O3`.

### 3. Proteção em Tempo Constante (Constant-Time)
Comparações e seleções críticas utilizam acumuladores OR e máscaras aritméticas de bits, evitando branches condicionais (`if`, `switch`) cuja latência dependente de dados possa ser explorada por canais laterais (*side-channel timing attacks*).

### 4. Resiliência a Fork
Em arquiteturas Unix/POSIX com chamadas concorrentes a `fork()`:
- O manipulador `pthread_atfork` detecta a criação do processo filho, zera o estado clonado e força novo reseed imediato com entropia do SO e jitter de temporizador.
- Chamadas `madvise` inseguras sobre ponteiros arbitrários de pilha ou heap não alinhados à página foram removidas da inicialização contextual, preservando segurança estrita sob AddressSanitizer e UndefinedBehaviorSanitizer.
- Em Windows, `VirtualLock` assegura que páginas contendo material criptográfico não sejam descarregadas em arquivos de swap.

### 5. Auto-Teste BIST (Built-In Self-Test)
Toda primeira inicialização do estado executa `kheiron_selftest()`. Caso ocorra divergência no bloco ChaCha20 com os parâmetros oficiais de teste da RFC 8439 ou falha de sanitização de pilha, o gerador trava em estado corrompido e recusa qualquer operação, retornando `-1`.

---

## Tabela Comparativa Empírica (Kheiron v2.0.0 vs. OpenSSL `RAND_bytes`)

Resultados medidos em ambiente Linux x86_64 comparando o Kheiron (AVX2 8 blocos com Forward Secrecy contínuo a cada 1 MB) diretamente contra o `RAND_bytes` do OpenSSL 3.x:

| Dimensão Avaliada | Kheiron v2.0.0 | OpenSSL (`RAND_bytes`) | Vantagem Relativa |
|:---|:---:|:---:|:---:|
| **Latência de blocos pequenos (64 B)** | **~455 MB/s** | ~128–144 MB/s | **3,16x a 3,54x superior** |
| **Throughput vetorial contínuo (1 KB)** | **~2.518 MB/s** | ~1.478 MB/s | **1,70x mais rápido** |
| **Footprint Binário** | **~35 KB** (objeto/executável) | ~7,76 MB (`libcrypto.so.3` + deps) | **~226x mais leve** |
| **Dependências Dinâmicas (`.so`)** | **0 (Zero)** | 10 objetos dinâmicos mapeados | **Zero dependências** |

---

## Comandos de Build e Teste

```bash
make check       # Executa suíte completa de testes E2E
make quick       # Executa testes rápidos de fumaça (--quick)
make bench       # Executa benchmark comparativo contra o OpenSSL
make sanitizer   # Bateria completa sob AddressSanitizer e UndefinedBehaviorSanitizer
make clean       # Limpa binários e arquivos gerados
```

---

## Métricas e Benchmarks Internos

Resultados obtidos executando `make test` em ambiente Linux x86_64 sob compilação com `-std=c99 -O2`:

| Métrica / Teste | Resultado Obtido | Status |
|:---|:---:|:---:|
| **Taxa de Geração Contínua (`kheiron_fill` — AVX2 Vetorial x86_64)** | **~2.518 MB/s (~2,5 GB/s)** | Aprovado |
| **Taxa de Geração Contínua (`kheiron_fill` — Escalar Fallback C99 puro)** | **~450 MB/s** | Aprovado |
| **Geração de Tokens (`kheiron_string`)** | **~4.800.000 strings/s** | Aprovado |
| **Ponto Flutuante (`kheiron_uniform_double`)** | **~6.600.000 valores/s** | Aprovado |
| **Embaralhamento (`kheiron_shuffle`)** | **~6.000.000 elem/s** | Aprovado |
| **Concorrência Lock-Free (8 threads)** | **< 1.0 ms (latência zero)** | Aprovado |
| **Vetor Oficial ChaCha20 (RFC 8439)** | **100% Coincidente** | Aprovado |
| **NIST SP 800-22 (Frequency Monobit Test)** | **p-value = 0.907** (limiar 0.01) | Aprovado |
| **NIST SP 800-22 (Runs Test)** | **p-value = 0.953** (limiar 0.01) | Aprovado |
| **Teste Qui-Quadrado ($\chi^2$)** | **$\chi^2 = 95.37$** (limiar 135.807) | Aprovado |
| **Fork Safety pós-clonagem** | **Bytes Divergentes** | Aprovado |

---

## Licença

Distribuído sob a licença MIT. Consulte `LICENSE` para mais detalhes.
