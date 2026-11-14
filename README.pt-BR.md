> [English Version](README.md)

# Kheiron v2.0.1 — CSPRNG Single-Header C99 de Alta Performance com Vetorização AVX2

[![CI](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml/badge.svg)](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/16676/badge)](https://www.bestpractices.dev/projects/16676)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/cainamatera/kheiron/badge)](https://scorecard.dev/viewer/?uri=github.com/cainamatera/kheiron)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

O **Kheiron** é um Gerador de Números Pseudo-Aleatórios Criptograficamente Seguro (CSPRNG) *single-header* e sem dependências, escrito em C99/C11, construído sobre a cifra de fluxo **ChaCha20** de 20 rounds (**RFC 8439**) com **vetorização paralela AVX2 de 8 blocos (~2,5 GB/s)**, despacho dinâmico via CPUID em runtime e fallback escalar portátil em C99 puro para qualquer arquitetura (ARM, RISC-V, x86 legado) sem exigir flags globais de compilação.

---

## Recursos Principais

- **Motor Vetorial AVX2 de 8 Blocos Paralelos**: Processa 512 bytes por iteração usando 16 registradores `__m256i` em CPUs `x86_64` compatíveis, com operações seguras de leitura/escrita desalinhadas.
- **Despacho Dinâmico via CPUID**: Detecta suporte a AVX2 automaticamente em tempo de execução via `__builtin_cpu_supports("avx2")` e alterna de forma transparente para execução escalar portátil em C99.
- **Active Forward Secrecy**: Rotação automática obrigatória de chave a cada 1 MB de saída gerada (`kheiron_rotate_key`), impedindo recuperação retrospectiva mesmo se o estado interno for comprometido posteriormente.
- **Entropia Fail-Closed do SO**: Dependência estrita dos CSPRNGs do sistema operacional (`getrandom`, `/dev/urandom`, `arc4random_buf`, `BCryptGenRandom`) — inicialização e reseed abortam com `-1` em caso de falha de entropia, nunca degradando silenciosamente para fontes fracas.
- **Amostragem Sem Viés**: Método de rejeição por multiplicação de 128 bits de Daniel Lemire (`__uint128_t`) para inteiros sem *modulo bias* e mapeamento exato de mantissa IEEE 754 (`[0.0, 1.0)`).
- **Sanitização de Memória Resistente a DSE**: `kheiron_secure_wipe` utiliza ponteiros `volatile` e barreiras de memória do compilador para impedir *Dead Store Elimination* sob `-O2`/`-O3`.
- **Resiliência a Fork e TLS Lock-Free**: Handlers `pthread_atfork` para reseed no processo filho (sem chamadas `madvise` arriscadas em ponteiros arbitrários de pilha/heap) e estado global thread-local (`KHEIRON_THREAD_LOCAL`) além de contextos explícitos `kheiron_ctx_t`.
- **Aleatoriedade Empírica Comprovada**: Verificado com PractRand v0.94 até 1 Terabyte (`2^40` bytes) com zero anomalias estatísticas em todos os 397 testes. Veja [docs/statistical_validation.md](docs/statistical_validation.md).

---

## Benchmark Comparativo Empírico (Kheiron vs. OpenSSL `RAND_bytes`)

Medido em Linux `x86_64` (AVX2 habilitado, com rotação ativa de *forward secrecy* a cada 1 MB):

| Métrica | Kheiron v2.0.1 | OpenSSL (`RAND_bytes`) | Vantagem Relativa |
| :--- | :---: | :---: | :---: |
| **Latência de Blocos Pequenos (64 B)** | **~455 MB/s** | ~128–144 MB/s | **3,16x–3,54x mais rápido** |
| **Throughput Vetorial Contínuo (1 KB)** | **~2.518 MB/s** | ~1.478 MB/s | **1,70x mais rápido** |
| **Pegada Binária** | **~35 KB** (estático/amalgamado) | ~7,76 MB (`libcrypto.so.3` + deps) | **~226x menor** |
| **Dependências Dinâmicas (`.so`)** | **0 (Zero)** | 10 objetos compartilhados mapeados | **Zero dependências** |

Veja [BENCHMARK.pt-BR.md](BENCHMARK.pt-BR.md) para a metodologia completa de medição.

---

## Quickstart e Integração

Em **exatamente uma** unidade de tradução `.c`, defina `KHEIRON_IMPLEMENTATION` antes de incluir o cabeçalho:

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
```

Nos demais arquivos `.c`, apenas inclua `"kheiron.h"`. Para emitir todas as rotinas como `static inline` sem exportar símbolos globais (ideal para isolamento entre múltiplos módulos), defina `KHEIRON_STATIC` antes de `#include "kheiron.h"`.

### Exemplo Completo

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>

int main(void) {
    uint8_t key[32];
    if (kheiron_fill(key, sizeof(key)) != 0) {
        return 1;
    }

    uint64_t roll = kheiron_range_u64(1, 6);
    double unit = kheiron_uniform_double();
    char token[17];
    kheiron_string(token, sizeof(token), KHEIRON_CHARSET_ALNUM);

    printf("Roll: %llu | Uniform: %.8f | Token: %s\n",
           (unsigned long long)roll, unit, token);

    kheiron_secure_wipe(key, sizeof(key));
    return 0;
}
```

Compile de forma limpa com C99:

```bash
gcc -std=c99 -O3 -Wall -Wextra -Werror example.c -o example
```

---

## Referência da API

### Inicialização e Contextos

| Função | Descrição |
| :--- | :--- |
| `int kheiron_ctx_init(kheiron_ctx_t *ctx)` | Inicializa um contexto explícito com entropia fail-closed do SO (`0` em sucesso, `-1` em falha). |
| `int kheiron_ctx_reseed(kheiron_ctx_t *ctx)` | Força o reseed imediato de um contexto explícito a partir da entropia do SO. |
| `void kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len)` | Preenche `buf` com `len` bytes criptograficamente seguros usando `ctx`. |
| `void kheiron_ctx_wipe(kheiron_ctx_t *ctx)` | Limpa de forma segura todo o estado interno de chave/nonce/contador de `ctx`. |

### Geração Global e Utilitários

| Função | Descrição |
| :--- | :--- |
| `int kheiron_fill(void *buf, size_t len)` | Preenche `buf` com `len` bytes usando o contexto global thread-local. |
| `uint32_t kheiron_u32(void)` / `uint64_t kheiron_u64(void)` | Retorna um inteiro sem sinal uniforme de 32 ou 64 bits. |
| `uint64_t kheiron_range_u64(uint64_t min, uint64_t max)` | Inteiro uniforme sem viés em `[min, max]` usando o método de 128 bits de Lemire. |
| `double kheiron_uniform_double(void)` | `double` IEEE 754 sem viés em `[0.0, 1.0)` (mantissa de 53 bits). |
| `float kheiron_uniform_float(void)` | `float` IEEE 754 sem viés em `[0.0f, 1.0f)` (mantissa de 24 bits). |
| `void kheiron_string(char *out, size_t len, const char *charset)` | Gera uma string aleatória terminada em nulo com `len - 1` caracteres de `charset`. |

### Primitivas de Segurança

| Função | Descrição |
| :--- | :--- |
| `void kheiron_secure_wipe(void *ptr, size_t len)` | Zeragem de memória resistente a DSE com escritas `volatile` e barreiras de compilador. |
| `void kheiron_rotate_key(kheiron_ctx_t *ctx)` | Rotaciona a chave e reinicia o contador para *forward secrecy* ativo (automático a cada 1 MB). |
| `int kheiron_bist(void)` | Executa o auto-teste embutido contra o vetor oficial ChaCha20 da RFC 8439. |

---

## Comandos de Build e Teste

| Comando | Descrição |
| :--- | :--- |
| `make check` | Executa a suíte completa de testes unitários, RFC 8439, BIST e resiliência a fork. |
| `make quick` | Executa testes rápidos de fumaça (*smoke tests*) para iteração ágil. |
| `make bench` | Compila e executa a suíte de benchmarks de vazão e latência. |
| `make stream` | Compila `bin/kheiron_stream` para baterias estatísticas externas (PractRand/TestU01). |
| `make sanitizer` | Compila e executa testes sob ASan + UBSan (`-fsanitize=address,undefined`). |
| `make clean` | Remove todos os binários compilados e artefatos de build. |

---

## Governança e Documentação

- [LICENSE](LICENSE) — Licença MIT
- [SECURITY.md](SECURITY.md) — Política de Divulgação de Vulnerabilidades e Versões Suportadas
- [BENCHMARK.pt-BR.md](BENCHMARK.pt-BR.md) — Metodologia Formal de Benchmark Comparativo
- [docs/statistical_validation.md](docs/statistical_validation.md) — Validação Estatística PractRand v0.94 (16 GB e 1 TB)
