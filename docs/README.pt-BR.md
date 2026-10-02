# Documentação

**Idioma:** [English](README.md) | Português (Brasil)

Este diretório separa orientação atual de arquitetura/desenvolvimento de
pesquisa e evidências históricas.

> Os documentos técnicos detalhados vinculados abaixo continuam sendo mantidos
> em inglês por enquanto. Este índice em português serve como ponto de entrada.

## Comece aqui

- [Guia rápido da API C pública](development/public-api-quickstart.md)
- [Build e testes](development/build-and-test.md)
- [Estrutura do repositório](development/repository-layout.md)
- [Índice de arquitetura](architecture/README.md)
- [Roadmap do projeto](../ROADMAP.md)
- [Especificações pre-v7 aposentadas](history/pre-v7-specs.md)
- [Como contribuir](../CONTRIBUTING.md)

## Arquitetura

- [Engine de CPU](architecture/cpu-engine.md)
- [Despacho de host services A32](architecture/a32-service-dispatch.md)
- [API C pública de embedding](architecture/public-embedding-api.md)
- [Compatibilidade JNI ARM32](architecture/a32-jni.md)
- [Compatibilidade libdl residente ARM32](architecture/a32-libdl.md)
- [Compatibilidade libm compartilhada ARM32](architecture/a32-libm.md)
- [Busca de bibliotecas de aplicação Android](architecture/a32-android-library-search.md)
- [Loader ELF32](architecture/elf32-loader.md)
- [Metadados dinâmicos ELF32](architecture/elf32-dynamic.md)
- [Metadados do linker](architecture/elf32-linker-metadata.md)
- [Arrays de ciclo de vida](architecture/elf32-lifecycle.md)
- [Strings do linker](architecture/elf32-linker-strings.md)
- [Resolução de dependências](architecture/elf32-dependency-resolution.md)
- [Carregamento de dependências](architecture/elf32-dependency-loading.md)
- [Resolução de símbolos](architecture/elf32-symbol-resolution.md)
- [Versionamento de símbolos](architecture/elf32-symbol-versioning.md)
- [Relocação](architecture/elf32-relocation.md)
- [Execução de fixture real](architecture/elf32-execution.md)
- [GNU RELRO](architecture/elf32-relro.md)

Notas adicionais de arquitetura de compatibilidade ficam junto desses
documentos em `architecture/` e cobrem superfícies de
libc/liblog/pthread/finalização/namespace.

## Desenvolvimento

- [Guia rápido da API C pública](development/public-api-quickstart.md)
- [Estrutura do repositório](development/repository-layout.md)
- [Build e testes](development/build-and-test.md)
- [Diagnósticos](diagnostics.md)
- [Política de segurança](../SECURITY.md)

## Pesquisa e evidências

`contracts/` contém os contratos de engenharia atuais aceitos.
`research/` contém notas de pesquisa e evidências específicas de ambientes.
Esses registros são contexto útil, mas não substituem os contratos aceitos nem
evidência de teste/CI da revisão exata.

Binários reais de terceiros usados em pesquisa de compatibilidade são entradas
de evidência; eles não são adicionados ao repositório a menos que os direitos de
redistribuição permitam explicitamente.
