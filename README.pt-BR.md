# liba32android

**Idioma:** [English](README.md) | Português (Brasil)

[![CI](https://github.com/millesant/liba32android/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/millesant/liba32android/actions/workflows/ci.yml)
[![API pública de embedding em C](https://github.com/millesant/liba32android/actions/workflows/public-embedding-api.yml/badge.svg?branch=main)](https://github.com/millesant/liba32android/actions/workflows/public-embedding-api.yml)

**liba32android** é um runtime experimental de compatibilidade AArch32 para
executar código nativo Android ARM de 32 bits dentro de um processo Android de
64 bits.

O projeto constrói deliberadamente as peças de baixo nível: execução de CPU
A32, memória guest lógica de 32 bits, carregamento/linkedição ARM ELF32,
serviços de compatibilidade Android e uma camada JNI limitada e controlada. O
runtime é agnóstico a jogos e aplicativos por design; particularidades de um
aplicativo não pertencem ao núcleo genérico.

> **Maturidade:** projeto ativo de pesquisa e engenharia. A API pública de
> embedding em C é versionada e testada, mas o projeto ainda não é uma camada de
> compatibilidade Android plug-and-play e não afirma compatibilidade geral com
> APKs ou jogos.

## Por que este projeto existe

Dispositivos Android modernos são majoritariamente 64 bits, enquanto uma grande
quantidade de software Android mais antigo ainda distribui código nativo
ARMv7/AArch32. O liba32android explora uma abordagem de runtime de
compatibilidade limpa, em vez de incorporar o comportamento de um aplicativo
específico em um fork de emulador ou loader.

O modelo de engenharia é intencionalmente orientado por evidências:

- fixtures Android ARM32 reais são geradas com uma versão fixada do Android NDK;
- comportamento de loader, linker, relocação, ciclo de vida e compatibilidade é
  limitado explicitamente e coberto por testes de regressão;
- builds Android arm64-v8a e requisitos de ELF/tamanho de página de 16 KiB são
  validados no CI;
- bibliotecas ARM32 reais são usadas como evidência de compatibilidade sem serem
  adicionadas ao repositório.

## O que funciona hoje

O runtime atual inclui:

- uma **API C pública v1**, instalável e versionada, para ciclo de vida do
  runtime, memória guest, execução ARM/Thumb limitada, captura exata de SVC e
  diagnósticos estruturados;
- execução A32 ARM/Thumb por meio de um adaptador privado para Dynarmic;
- endereços virtuais guest lógicos de 32 bits, com memória mapeada e caminhos
  baseados em callbacks;
- mapeamento validado de ARM ELF32 `ET_EXEC` / `ET_DYN` e posicionamento
  dinâmico limitado;
- metadados dinâmicos, carregamento de dependências, busca de símbolos, hashes
  SysV/GNU, versionamento de símbolos, relocação, PLT `R_ARM_JUMP_SLOT` e
  selagem GNU RELRO;
- planejamento e execução limitada do ciclo de vida de construtores/destrutores;
- política de namespace/bibliotecas de plataforma Android e busca de bibliotecas
  ciente do chamador;
- superfícies parciais de compatibilidade para libc, liblog, libdl, libm,
  sincronização no estilo pthread, `__aeabi_atexit` / `__cxa_finalize` e
  serviços relacionados;
- bootstrap JNI VM/GetEnv/JNI_OnLoad, mais FindClass, RegisterNatives e
  despacho reverso limitado de natives sem argumentos Java;
- fixtures Android ARMv7 reproduzíveis, além de probes de espaço de
  endereçamento/runtime no Android.

O build normal registra dezenas de regressões CTest no host, com workflows
adicionais de fixtures/integração no GitHub Actions.

## O que não é afirmado

O projeto ainda não é:

- uma Java VM completa ou substituto do Android Runtime;
- uma implementação JNI completa;
- uma implementação completa de Bionic/libc/pthread/TLS;
- um modelo completo de linker/filesystem/APK do Android;
- uma pilha de compatibilidade para gráficos, áudio, input ou framework;
- uma garantia de que qualquer APK ou jogo legado vai funcionar;
- uma ABI estável de produção para os internals privados C++ de ELF/compatibilidade.

A API C pública é intencionalmente bem menor que o runtime interno enquanto
essas camadas ainda evoluem.

## Início rápido

Os requisitos de host usados pelo CI são CMake 3.24+, Ninja, um compilador
C++20, headers do Boost, binutils e headers de desenvolvimento do zlib.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

A biblioteca compartilhada é gerada como:

```text
build/liba32android.so
```

O header público é:

```text
include/liba32android/liba32android.h
```

Para preparar uma instalação:

```sh
cmake --install build --prefix /tmp/liba32android-install
```

Veja o [guia rápido da API C pública](docs/development/public-api-quickstart.md)
para um exemplo de chamador C externo e o comando de compilação.

## Arquitetura em resumo

```text
API de embedding no host
       |
       v
orquestração do runtime + serviços do host
       |
       +----> GuestMemory lógica
       |
       +----> adaptador de CPU A32 ----> Dynarmic
       |
       +----> loader/linker ARM ELF32
       |         |
       |         +----> dependências / símbolos / relocações / RELRO
       |
       +----> serviços de compatibilidade Android
                 |
                 +----> libc / liblog / libdl / libm / pthread / JNI
```

As regras de camadas importam: ponteiros do host nunca viram ponteiros guest,
Dynarmic não escapa de `src/cpu/` e comportamento específico de plataforma
fica acima dos contratos genéricos de CPU/memória/ELF.

Comece por [docs/README.pt-BR.md](docs/README.pt-BR.md) e pelo
[índice de arquitetura](docs/architecture/README.md). A documentação técnica
detalhada continua sendo mantida em inglês por enquanto.

## Estrutura do repositório

```text
include/liba32android/   API C pública e estável de embedding
src/public/              implementação da API pública
src/cpu/                 abstração de CPU A32 e adaptador Dynarmic
src/runtime/             orquestração genérica de execução/serviços
src/memory/              implementações de memória guest
src/elf/                 camadas de loader/linker ARM ELF32
src/compat/              serviços/adaptadores de compatibilidade Android

tests/                   regressões unitárias e de integração
tools/fixtures/          builders de fixtures ARM32 reproduzíveis
tools/android/           harnesses de diagnóstico/validação Android
docs/                    arquitetura, desenvolvimento e notas de pesquisa
.agent/specs/            contratos internos atuais aceitos do projeto
```

Para regras de ownership/dependências, veja
[docs/development/repository-layout.md](docs/development/repository-layout.md).

## Roadmap

O roadmap público está em [ROADMAP.md](ROADMAP.md). A trilha imediata de
compatibilidade está avançando JNI a partir da etapa concluída de
classe/registro de natives para identidade de classes/membros, seguida por
referências, strings/arrays, exceções, chamadas de métodos/campos e superfícies
de thread/VM.

## Contribuindo

Contribuições são bem-vindas. Comece por
[CONTRIBUTING.md](CONTRIBUTING.md), que cobre requisitos de build/teste,
limites de escopo e o que torna útil um bug report ou uma contribuição de
compatibilidade.

As invariantes de engenharia específicas do repositório para mantenedores
automatizados ficam em [AGENTS.md](AGENTS.md). Contribuidores humanos **não**
precisam de acesso ao repositório externo de automação/control plane do
mantenedor.

Para relatórios sensíveis de segurança, veja [SECURITY.md](SECURITY.md). As
expectativas da comunidade estão em [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Estado do projeto

Os contratos técnicos atuais aceitos ficam em `.agent/specs/`; o estado
observado de implementação/validação é resumido em `.agent/STATE.md`, e o
próximo trabalho em ordem de dependência fica em `.agent/NEXT.md`. A antiga
árvore de especificações pre-v7 continua disponível no histórico Git; veja a
[nota histórica](docs/history/pre-v7-specs.md).

## Licença

liba32android é licenciado sob a [Apache License 2.0](LICENSE).

Dependências de terceiros mantêm suas próprias licenças. O projeto não inclui no
repositório os binários ARM32 reais fornecidos e usados como evidência durante
pesquisa de compatibilidade.
