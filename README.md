# DragonBoardVR

DragonBoardVR é um plugin nativo para Skyrim VR que transforma um painel físico
preso à mão em uma interface interativa para inventário, magias, journal,
configurações, ações de outros mods e informações do jogo.

O Board combina objetos 3D da scene graph do Skyrim com páginas RmlUi
renderizadas em texturas Direct3D 11. A interação pode acontecer por laser,
toque frontal com o dedo e grab físico, preservando o contexto da mão usada.

> O projeto está em desenvolvimento ativo. O alvo é exclusivamente Skyrim VR;
> Skyrim Special Edition e Anniversary Edition não são suportados.

## Escopo

O projeto inclui:

- Board físico normalmente anexado à mão não dominante;
- páginas RmlUi para Inventory, Magic, Journal, Settings, Developer, Mods e
  edição de itens;
- interação por laser e por toque frontal com o dedo;
- posicionamento livre do Board, com rotação, escala por dois grips e
  persistência relativa ao nó da mão;
- pin de itens, magias e widgets no Board, nas mãos ou no mundo;
- equip e desequip por mão física;
- previews 3D de itens;
- marcadores de mapa e de objetivos de quests;
- editor de INIs dos mods ativos no Mod Organizer 2;
- teclado virtual para edição de valores;
- páginas RmlUi registradas por plugins externos por API C++ ou Papyrus;
- integração opcional com HIGGS, VRIK e Spell Wheel VR;
- renderização RmlUi por estado sujo, virtualização de listas e proteção do
  estado D3D11;
- preview/editor RmlUi standalone para desenvolvimento sem abrir o Skyrim;
- scanner auxiliar read-only para descobrir INIs no perfil ativo do MO2.

### Fora do escopo atual

- suporte a Skyrim SE ou AE;
- substituição do Mod Organizer 2;
- captura ou hospedagem pública de superfícies da PrismaUI;
- API pública completa para widgets, superfícies e botões físicos
  independentes. Partes dessa API estão reservadas, mas ainda não são
  anunciadas como capacidades funcionais;
- garantia de compatibilidade com qualquer mod que altere a scene graph,
  controles ou renderização do Skyrim VR.

## Tecnologias

| Área | Tecnologia |
| --- | --- |
| Runtime | Skyrim VR e SKSE VR |
| Linguagem | C++23 |
| Biblioteca de jogo | CommonLibSSE-NG |
| Interface | RmlUi 6.2 |
| Renderização | Direct3D 11 |
| Build | xmake 3.0.1, MSVC e Ninja |
| Configuração | SimpleIni, INI e JSON |
| Extensões | API C++ e Papyrus |

## Estrutura principal

```text
Assets/                 RML, RCSS, meshes, texturas, INIs e ferramentas
Src/bootstrap/          inicialização, logging e lifecycle do SKSE
Src/game/               ações executadas no Skyrim
Src/integrations/       integrações opcionais
Src/papyrus/            API Papyrus
Src/ui/                 controllers, input, RmlUi, páginas e widgets
Src/vrui/               painéis, containers, raycast, layout e settings VR
Tools/IniScanner/       scanner read-only e testes do editor de INI
Tools/RmlPreview/       preview/editor RmlUi standalone
docs/                   arquitetura, integração e trabalho pendente
```

Uma descrição mais detalhada está em
[`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md). A integração para mods
externos é documentada em
[`docs/RMLUI_INTEGRATION.md`](docs/RMLUI_INTEGRATION.md).

## Requisitos para build

- Windows 10 ou 11 x64;
- Visual Studio 2022 com **Desktop development with C++** e Windows SDK;
- xmake 3.0.1 ou mais recente;
- Git;
- Python e PyInstaller somente para reconstruir o scanner de INIs;
- Skyrim VR e SKSE VR para testes dentro do jogo.

O `xmake.lua` espera estas dependências locais:

```text
lib/commonlibsse-ng/
ClibUtil/include/
simpleini/
xbyak/
```

Elas podem ser diretórios normais ou junctions do Windows. Não são incluídas
como cópias locais neste repositório. RmlUi e as dependências declaradas no
`xmake-requires.lock` são resolvidas pelo xmake.

Exemplo de junctions, ajustando os caminhos para a sua instalação:

```powershell
New-Item -ItemType Junction -Path .\lib -Target C:\deps\lib
New-Item -ItemType Junction -Path .\ClibUtil -Target C:\deps\ClibUtil
New-Item -ItemType Junction -Path .\simpleini -Target C:\deps\simpleini
New-Item -ItemType Junction -Path .\xbyak -Target C:\deps\xbyak
```

O diretório apontado por `lib` deve conter `commonlibsse-ng`.

## Build do plugin

Abra um PowerShell na raiz do repositório:

```powershell
xmake f -p windows -a x64 -m releasedbg `
  --skyrim_vr=y --skyrim_se=n --skyrim_ae=n
xmake build DragonBoardVR
```

O projeto rejeita configurações SE/AE. O build principal produz a DLL e
sincroniza os arquivos instaláveis em:

```text
install_output/
  meshes/
  Scripts/
  SKSE/Plugins/
  textures/
```

O `after_build` preserva INIs que já existam em `install_output`. Para produzir
um pacote de release, use uma saída limpa ou revise o diretório antes de
compactá-lo, pois ele não funciona como uma staging directory descartável.

### Build limpo

```powershell
xmake clean DragonBoardVR
xmake f -p windows -a x64 -m releasedbg `
  --skyrim_vr=y --skyrim_se=n --skyrim_ae=n
xmake build DragonBoardVR
```

## Preview RmlUi

O preview usa o renderer RmlUi/D3D11 do projeto sem carregar Skyrim, SKSE ou
CommonLib:

```powershell
xmake build DragonBoardRmlPreview
xmake run DragonBoardRmlPreview
```

Para abrir uma página específica:

```powershell
xmake run DragonBoardRmlPreview .\Assets\ui\rml\inventory.rml
```

Controles e recursos do editor estão descritos em
[`Tools/RmlPreview/README.md`](Tools/RmlPreview/README.md).

## Testes

### Listas virtuais e animações

```powershell
xmake build RmlVirtualListTests
.\build\windows\x64\releasedbg\RmlVirtualListTests.exe

xmake build RmlEntranceAnimationTests
.\build\windows\x64\releasedbg\RmlEntranceAnimationTests.exe
```

### Editor e scanner de INIs

```powershell
python -m unittest discover -s Tools\IniScanner\tests -v

xmake build IniEditorTests
.\build\windows\x64\releasedbg\IniEditorTests.exe
```

Para reconstruir o executável distribuído com o mod:

```powershell
.\Tools\IniScanner\BuildScanner.ps1 -OutputDirectory .\Assets\tools
```

O scanner apenas lê o perfil do MO2 e gera um catálogo. Validação de caminhos,
backup e escrita atômica dos INIs permanecem sob responsabilidade do plugin
C++.

## Instalação para teste

Após o build, copie o conteúdo de `install_output` para um mod separado no MO2
ou para a árvore `Data` usada no ambiente de desenvolvimento.

Antes de substituir uma instalação ativa:

1. feche o Skyrim VR;
2. faça backup da DLL e dos INIs ativos;
3. copie os artefatos;
4. compare o hash da DLL compilada com a DLL instalada;
5. reinicie o Skyrim VR e valide o comportamento dentro do jogo.

Build bem-sucedido, preview funcional e hashes iguais provam a consistência dos
artefatos, mas não provam o comportamento em runtime.

## Documentação

- [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md): implementação atual;
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): limites e evolução da
  arquitetura;
- [`docs/RMLUI_INTEGRATION.md`](docs/RMLUI_INTEGRATION.md): API e integração de
  páginas externas;
- [`docs/TODO.md`](docs/TODO.md): pendências e critérios de conclusão;
- [`KNOWLEDGEBASE.md`](KNOWLEDGEBASE.md): notas técnicas e investigações.

## Licença

O manifest de build declara o projeto sob a licença GPL-3.0.
