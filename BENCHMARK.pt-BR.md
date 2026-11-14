> [English](BENCHMARK.md)

# Metodologia Formal de Benchmark — Kheiron v2.0.x vs. OpenSSL 3.x

Este documento define a metodologia reproduzível utilizada para avaliar vazão (*throughput*), latência e pegada binária do **Kheiron** (`kheiron.h`) em comparação direta com o **OpenSSL 3.x** (`RAND_bytes` e `EVP_chacha20`).

## Especificação do Ambiente de Teste

- **Arquitetura / CPU**: `x86_64` com suporte às instruções **AVX2 / FMA** (despacho dinâmico em runtime via CPUID).
- **Compilador**: GCC e Clang com flags estritas (`-O3 -mavx2`, além de validação padrão sob `-std=c99 -O2 -Wall -Wextra -Wpedantic -Werror`).
- **Sistema Operacional**: Linux `x86_64` (ambiente de execução AVX2).
- **Base de Referência**: OpenSSL 3.x (`libcrypto.so.3` / `RAND_bytes`).

## Protocolo de Medição e Isolamento

1. **Temporização de Alta Precisão**: As medições utilizam contadores de ciclo de hardware (`RDTSC`) e `clock_gettime(CLOCK_MONOTONIC_RAW)` / `CLOCK_MONOTONIC` para evitar distorções por ajustes do relógio do sistema.
2. **Aquecimento de Cache**: Cada suíte de benchmark executa iterações de aquecimento antes de entrar na janela cronometrada, garantindo que instruções e buffers quentes residam na cache L1/L2.
3. **Múltiplas Repetições**: Múltiplas rodadas e milhares de iterações por tamanho de bloco são executadas para descartar ruído de escalonamento do SO.
4. **Invariantes Criptográficas Ativas**: Todas as medições do Kheiron executam com **Forward Secrecy ativo** (rotação automática de chave a cada 1 MB), sanitização segura de memória (`kheiron_secure_wipe`) e proteção pós-`fork` habilitada.

## Granularidades Avaliadas

- **Latência de Blocos Pequenos (`64 B`)**: Modela geração de chaves, nonces, IVs e tokens curtos — onde o Kheiron AVX2 atinge **~455 MB/s** (**3,16x a 3,54x** mais rápido que o `RAND_bytes` do OpenSSL).
- **Throughput Vetorial Contínuo (`1 KB` e superiores)**: Avalia a ocupação total do pipeline paralelo AVX2 de 8 blocos (512 bytes/iteração), atingindo **~2.518 MB/s (~2,5 GB/s)** em `1 KB` (**1,70x** mais rápido que o `RAND_bytes`).

## Throughput por Tamanho de Buffer

| Tamanho de Buffer | Kheiron AVX2 (v2.0.1) | OpenSSL 3.x (`RAND_bytes`) | Speedup Relativo | Caso de Uso Principal |
| :--- | :--- | :--- | :--- | :--- |
| **16 B** | ~180 MB/s | ~55 MB/s | **~3,27x** | UUIDs, Salts, Nonces |
| **64 B** | **~455 MB/s** | ~128–144 MB/s | **~3,35x** | Chaves Simétricas (ChaCha/AES), IVs |
| **256 B** | ~1.250 MB/s | ~610 MB/s | **~2,05x** | Derivação de seeds, sessões TLS |
| **1 KB** | **~2.518 MB/s** | ~1.478 MB/s | **~1,70x** | Cifragem de pacotes em lote, mistura de stream |
| **64 KB** | ~2.550 MB/s | ~1.820 MB/s | **~1,40x** | Streaming contínuo / throughput em pipe |

## Comparativo de Pegada Binária

| Dimensão | Kheiron (Single-Header) | OpenSSL 3.x (`libcrypto.so.3`) |
| :--- | :---: | :---: |
| **Pegada em Disco** | **~35 KB** (binário estático/amalgamado) | **~7,76 MB** (biblioteca + dependências) |
| **Dependências Dinâmicas (`.so`)** | **0 (Zero)** | **10** objetos compartilhados mapeados |

## Reprodução Local

```bash
make bench
```
