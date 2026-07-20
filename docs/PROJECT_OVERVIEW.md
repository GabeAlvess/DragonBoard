# DragonBoardVR — visão geral do projeto

## Finalidade deste documento

Este arquivo descreve o estado atual do DragonBoardVR a partir do código existente. Ele deve ser usado por agentes e contribuidores para localizar responsabilidades, entender os fluxos reais e preservar os contratos de interação do mod.

Para trabalho pendente, prioridades e critérios de conclusão, consulte `TODO.md`. Para a avaliação prescritiva da arquitetura, ownership, regras de dependência e plano incremental de evolução, consulte `ARCHITECTURE.md`. Este arquivo se limita a explicar o sistema atual.

## Snapshot analisado

- Data da análise: 2026-07-20.
- Branch: `codex/finger-touch-diagnostic`.
- Commit HEAD: `73a630d` (`feat: extend RmlUi panels and VR integrations`).
- O working tree contém alterações locais e arquivos ainda não versionados, principalmente a experiência de toque por dedo, integração VRIK e diagnóstico de finger tracking.
- Escopo lido: `xmake.lua`, 95 headers e 92 fontes C++ em `Src/`, 8 documentos RML, 10 folhas RCSS, 2 scripts Papyrus, ferramentas locais e documentação existente.
- Tamanho aproximado: 35.755 linhas C++ em `Src/`, 5.100 linhas RML/RCSS e 2.742 linhas C++ nas ferramentas.
- Esta análise confirma estrutura e comportamento implementado no código. Ela não substitui build limpo, conferência do DLL implantado nem teste dentro do Skyrim VR após reiniciar o jogo.

## Resumo executivo

DragonBoardVR é um plugin nativo exclusivo para Skyrim VR que constrói um Board físico, normalmente ligado à mão não dominante, e o usa como uma camada direta sobre menus e ações do jogo. O sistema combina objetos 3D na scene graph do Skyrim com documentos RmlUi renderizados em texturas D3D11.

Os pilares implementados são:

- acesso rápido a mapa, inventário, magia, journal, estatísticas, menus nativos e comandos;
- personalização de posição, rotação e escala de painéis, botões, itens e superfícies persistentes;
- pin de itens e magias no Board, na mão e no mundo, com widgets persistentes que podem sobreviver ao fechamento do Board;
- painéis RML internos e painéis de página registrados por mods externos;
- renderização RmlUi otimizada por dirty state, resolução configurável e virtualização das listas de inventário e magia;
- integração opcional com HIGGS, VRIK e Spell Wheel VR;
- APIs C++ e Papyrus para painéis RML de página.

O projeto ainda está em desenvolvimento ativo. A API externa de páginas funciona, mas a API pública de superfícies/widgets independentes está somente reservada: os métodos existem na interface v2, não são anunciados em `GetCapabilities()` e retornam falha. Também não existe, hoje, uma API pública para um mod registrar um botão físico do Board que abra o seu painel. Os oito slots e botões configuráveis atuais pertencem ao sistema interno de INI/layout.

## Tecnologias e alvo

| Área | Implementação atual |
| --- | --- |
| Runtime | Skyrim VR / SKSE VR, sem alvo SE ou AE |
| Linguagem | C++23 |
| Biblioteca de jogo | CommonLibSSE-NG |
| UI 2D | RmlUi 6.2, link estático, sem Lua/SVG/Lottie |
| Render | Direct3D 11, render target próprio e hook de `IDXGISwapChain::Present` |
| Build | xmake 3.0.1, MSVC + Ninja |
| Persistência | INI via SimpleIni e layout JSON via nlohmann/json |
| Scripts | Papyrus nativo registrado pelo SKSE |
| Integrações opcionais | HIGGS, VRIK e Spell Wheel VR |

`xmake.lua` força `skyrim_vr=true`, desativa SE/AE, define `ENABLE_SKYRIM_VR` e rejeita combinações incompatíveis. O target principal é `DragonBoardVR`; existem também `DragonBoardRmlPreview` e `RmlVirtualListTests`.

## Mapa de execução

```text
SKSEPlugin_Load
  -> logging, SKSE::Init, Papyrus e listener de mensagens
  -> kPostPostLoad: integrações HIGGS/VRIK/Spell Wheel
  -> kInputLoaded: input sink e warm-up RmlUi
  -> kDataLoaded: ações, mod events, hotkeys e retry do warm-up
  -> VRFrameUpdater recebe input e agenda trabalho na game thread
       -> VRMenuManager / FrameUpdateController
       -> scene graph, painéis 3D, gameplay e snapshots RML
  -> hook de Present
       -> comandos DOM e input copiado
       -> RmlUi Update/Render em render targets D3D11
       -> eventos copiados de volta para a game thread
```

### Domínios de thread

Há três fronteiras importantes:

1. O input sink recebe eventos de controle e agenda um único update pendente pela task interface do SKSE.
2. A game thread é dona de CommonLib, objetos do Skyrim, scene graph, equip, quests, snapshots e callbacks externos.
3. A thread de `Present` é dona de RmlUi, documentos, DOM, render targets e envio de input para os contexts.

`RmlInputBridge`, filas, atomics e mutexes transportam estado entre os dois últimos domínios. Um listener RmlUi não deve chamar gameplay/CommonLib diretamente. Eventos externos são copiados no `Present` e despachados depois na game thread.

## Organização do código

| Caminho | Responsabilidade |
| --- | --- |
| `Src/main.cpp` | Entrada SKSE, registro Papyrus, messaging e exports das APIs C++ |
| `Src/bootstrap/` | Logging, mensagens de lifecycle e resolução de hotkeys |
| `Src/core/` | Cooldown e fila de tarefas adiadas sem dependência de UI |
| `Src/runtime/vr/` | Operações específicas do runtime: menus, haptics, console e transform de referências |
| `Src/game/actions/` | Parse e execução unificada de console, cast e equip para Mods/pins |
| `Src/gameplay/` | Slow time durante combate enquanto o Board está aberto |
| `Src/ui/menu/` | Composição do Board, inicialização, abertura/fechamento e apresentação dos painéis |
| `Src/ui/input/` | Modos de ativação, trigger/grip, hover, long press, ponteiro e toque por dedo |
| `Src/ui/panels/` | Registro, troca, transform, pin do Board e watchdog de handoff |
| `Src/ui/widgets/` | Criação/refresh/ação dos widgets fixos e pinados |
| `Src/ui/refresh/` | Coordenação de refresh de listas, transforms e estado equipado |
| `Src/ui/equipment/` | Cooldown, refresh e mudanças de equipamento protegidas para o skeleton |
| `Src/ui/settings/` | Debounce de salvamento e detecção de edição externa do INI |
| `Src/ui/rml/` | Host RmlUi, renderer D3D11, páginas, superfícies, presenters, input e métricas |
| `Src/vrui/` | Núcleo histórico de widgets/painéis 3D, raycast, inventário, magia, layout e settings |
| `Src/papyrus/` | API nativa de painéis e alias de registro do gesto VRIK |
| `Src/integrations/` | Integrações opcionais com Spell Wheel e VRIK |
| `Src/diagnostics/` | Sondas de diagnóstico, atualmente finger tracking |
| `Assets/ui/rml/` | RML, RCSS, fontes e imagens dos painéis internos |
| `Tools/RmlPreview/` | Editor/preview standalone e teste de lista virtual |

`Src/ui/*` vem extraindo responsabilidades de `VRMenuManager`, mas o sistema ainda é híbrido: os widgets e previews 3D continuam em `Src/vrui/*`, enquanto as telas principais novas estão em `Src/ui/rml/*`.

## Lifecycle do Board

`VRFrameUpdater` espera o player, cell, 3D e skeleton estabilizarem antes de inicializar o menu. Ele identifica mão esquerda/direita e mão do menu/dominante, traduz eventos de controle para `VRMenuManager` e encaminha trigger/grip ao host RmlUi.

`VRMenuManager` é a fachada central usada pelo restante do plugin. A implementação de cada responsabilidade é delegada para controllers em `Src/ui/`:

- `MenuInitializationController` carrega settings/layout e resolve os nós de mão, cabeça, skeleton, laser e reticle;
- `MenuComposition` cria painéis, botões fixos, oito slots, containers persistentes, marcadores e editor de item;
- `MenuLifecycleController` abre/fecha o Board, aplica slow time em combate, haptics e visibilidade;
- `PanelManagementController` registra, troca, prende e reposiciona painéis;
- `FrameUpdateController` ordena tarefas adiadas, integração Spell Wheel, equip, input, refresh, panels e RmlUi.

O Board segue a scene graph do player. Acompanhar a mão significa anexar a hierarquia do painel aos nós do skeleton/controlador; isso não é equivalente a uma superfície apenas `world pinned`.

## Painéis e superfícies RmlUi

### Superfície principal

`RmlPanelHost` mantém uma superfície principal de página anexada ao Board. Ela usa um context RmlUi compartilhado e exibe um único documento de página por vez. Inventory, Magic, Journal, Settings, Developer, Mods, Item Editor e painéis externos usam essa mesma superfície física e o mesmo render target.

Ao trocar de página, os outros documentos são escondidos. O quad acompanha o transform real do Board; UV, pointer e preview permanecem alinhados com a geometria em movimento.

### Superfície de status

O status é a primeira superfície independente interna. Ele possui context, render target, SRV, NIF e ponte de textura próprios. Exibe localização, ouro e peso, aparece no Home e é escondido enquanto uma página principal está ativa.

`RmlSurface` encapsula o context/render básico e `RmlSurfaceGrabController` implementa posição/rotação com uma mão e escala uniforme com duas mãos. O transform é persistido no container reservado `RmlUiSurfaces` do layout JSON.

Cada nova superfície precisa de um caminho de textura/NIF intrínseco exclusivo. O Skyrim reutiliza recursos pelo caminho da textura; compartilhar o mesmo NIF/diffuse pode fazer um widget amostrar a textura de outra página.

### Documentos internos

| Documento | Estado funcional representado no código |
| --- | --- |
| `inventory.rml` | Lista virtualizada, busca, filtros, preview 3D, equip/desequip por mão, drop, favorite e pin |
| `magic.rml` | Lista virtualizada, busca, filtros por escola/tipo, preview, equip, edit e pins no Board/mão/mundo |
| `journal.rml` | Quests, objetivos, tracking, estatísticas e integração com o marcador de quest |
| `settings.rml` | Edição de transforms e escalas principais, edit mode, developer button e world pin |
| `dev.rml` | Comandos, informações do jogo, métricas RmlUi e calibração do mapa |
| `mods.rml` | Ações gravadas/configuradas pelo usuário |
| `edit.rml` | Edição por item/categoria, preview 3D e destinos de pin |
| `status_widget.rml` | Widget independente de localização, ouro e peso |

Lookup dos documentos internos:

1. `Data/SKSE/Plugins/DragonBoardVR/ui/`;
2. `SKSE/Plugins/DragonBoardVR/ui/`;
3. `Assets/ui/rml/` para desenvolvimento local.

Quando uma tela RML interna não está disponível, alguns callers ainda tentam o painel 3D clássico correspondente. Essa coexistência é importante ao diagnosticar diferenças entre o visual esperado e o caminho realmente aberto.

## Inventário, magia e preview 3D

Os containers históricos `VRUIInventoryContainer` e `VRUIMagicContainer` continuam sendo os backends que consultam o player, constroem snapshots e executam ações. `RmlInventoryPresenter` e `RmlMagicPresenter` copiam esses snapshots para modelos planos, preservam seleção/busca e fazem a tradução entre índice visível e índice real.

As listas RML materializam no máximo dez linhas para a viewport atual. A seleção, busca, filtros, hover, long press e ações mantêm índices reais. O teste standalone cobre datasets de 0, 25, 75, 250, 500 e 1000 entradas.

`VRUIItemEditPanel` continua dono do objeto 3D vivo. Em modo RML, controles antigos são escondidos, mas o preview selecionado permanece anexado atrás da superfície. Alterações de position/rotation/scale são sincronizadas entre preview e draft RML e persistidas por item ou categoria.

Overrides de item novos usam uma chave estável `Plugin.esp|LOCAL_FORM_ID`; FormIDs completos antigos continuam legíveis para compatibilidade.

Contrato de mão:

- trigger esquerdo equipa ou desequipa a mão esquerda;
- trigger direito equipa ou desequipa a mão direita;
- cópias idênticas precisam ser resolvidas por `ExtraDataList`, `kWornLeft` e `kWorn`, não apenas pelo base form;
- mudanças de arma/armadura que afetam o skeleton passam por `EquipInteractionController`.

Esse contrato deve ser validado no jogo com duas cópias idênticas e com ambas as mãos; build e log não bastam.

## Pins e widgets persistentes

O editor aceita, conforme o tipo do item:

- pin no dashboard/Board;
- pin na mão esquerda;
- pin no mundo;
- pin com label visível/oculta.

O layout também modela `pinToHmdWorld`, usado pelos containers persistentes. `AlwaysVisiblePanel` e `AlwaysVisibleHmdPanel` mantêm widgets fora da página ativa e podem continuar visíveis com o Board fechado, conforme a categoria do pin.

Pins novos armazenam o transform visual composto (`visualTransformComposed`) para preservar a pose vista no preview. O layout antigo continua legível e é convertido quando o item é pinado novamente.

Widgets fixos podem representar item, magia ou uma ação serializada. `FixedWidgetActionHandler` executa equip/cast/comando e contém o fluxo especial de livros, incluindo spawn e grab via HIGGS quando disponível.

## Mapa, journal e estatísticas

O painel de fundo contém marcador do player e marcador do objetivo de quest. `VRUIMapMarker` converte posição do mundo para a superfície do mapa e suporta calibração por cinco landmarks. O Developer panel captura pontos no exterior e calcula o transform de calibração.

O Journal captura quests, objetivos e estatísticas do player. Alvos móveis são guardados por `ObjectRefHandle` e atualizados periodicamente; resolução cara de entrada permanece orientada a evento. A seleção do marcador é persistida no INI e solicitada novamente em `kPostLoadGame` e `kNewGame`.

As estatísticas de performance ficam no Developer panel: FPS, frame time, tempos de Update/BeginFrame/Render/EndFrame, grupos do state guard D3D11, draw calls, elementos DOM, renders por segundo, frames em cache, resolução, documento ativo e motivo dirty.

## Input e interação VR

Modos globais de ativação:

- Grip;
- Trigger;
- Thumbstick;
- Grip + Thumbstick;
- Grip + Y;
- Grip + B;
- Hotkey 8.

No RmlUi, trigger clica/arrasta slider e grip controla scroll por movimento do controller ou thumbstick. Elementos externos interativos precisam ter `id`; `button` e `input` presentes durante o load são descobertos automaticamente. Controles adicionados depois por substituição dinâmica de markup não entram automaticamente no fallback geométrico.

O pointer usa raycast, UV, reticle e haptics. `RmlInputBridge` conserva a mão do último trigger para ações hand-specific. `FingerTouchController`, `FingerTrackingProbe` e `VrikFingerPose` formam uma experiência alternativa de toque direto, mas essa frente ainda está local/não versionada e deve ser tratada como trabalho em validação.

## Personalização e persistência

| Arquivo | Conteúdo |
| --- | --- |
| `Data/SKSE/Plugins/DragonBoardVR.ini` | Ativação, mão do menu, visual, RmlUi, interação, botões, oito slots, itens, mapa, quest marker e debug |
| `Data/SKSE/Plugins/DragonBoardVR_Layout.json` | Containers, elementos, position/rotation/matrix/scale, visual, pin, label e transforms das superfícies |
| `Data/SKSE/Plugins/DragonBoardVR_Mods.ini` | Ações gravadas: label, NIF e comando serializado |
| `Data/SKSE/Plugins/DragonBoardVR_DevCommands.ini` | Lista de comandos do Developer panel |

O INI é observado periodicamente; edições externas pedem refresh. Saves feitos pelo próprio mod atualizam o baseline do watcher para evitar rebuild indevido. O layout JSON é usado para transforms manipulados no jogo. Ações Mods são salvas em background com uma geração para impedir que um snapshot antigo sobrescreva um novo.

## Botões e ações configuráveis atuais

`MenuComposition` cria botões fixos para Skills, Inventory, Magic, Settings, Save, Mods, Journal, Map e opcionalmente Developer. Label, action, position, rotation e scale são configuráveis no INI e os transforms também podem vir do layout JSON.

Existem oito slots. Cada slot aceita action, imagem, NIF, label, sublabel, transform e modo floating. O resolver interno reconhece painéis DragonBoard, menus do jogo, quicksave, comandos de console e ações gravadas. Long press em botões fixos pode alterar o painel padrão.

Isso é personalização interna do usuário, não uma extensão formal da API. Um mod externo ainda não consegue chamar `RegisterButton()` nem associar declarativamente um botão físico ao handle de seu painel.

## API externa

### C++ v1

`RequestPluginAPI()` retorna `IDragonBoardVR`, que oferece:

- register/unregister/show/hide/visible;
- `SetElementText`;
- `SetElementAttribute`;
- `SetElementClass`;
- callback de `Click` e `Change` na game thread.

### C++ v2

`RequestPluginAPI2()` adiciona descriptor versionado, `GetInterfaceVersion`, `GetCapabilities` e `GetPanelState`.

Capabilities anunciadas no build atual:

- `VersionedDescriptors`;
- `ThreadSafeDomUpdates`;
- `PanelLoadState`;
- `ElementEvents`.

Capabilities não anunciadas:

- `IndependentSurfaces`;
- `SimultaneousSurfaces`;
- `GrabbableSurfaces`.

`CreateSurface`, `DestroySurface`, `BindPanelToSurface`, visibilidade e transform estão reservados na vtable, mas são stubs. Portanto, mods externos criam páginas principais, não widgets arbitrários.

### Papyrus

O script hidden `DragonBoardVR.psc` expõe instalação, toggle, register/unregister/show/hide, visibilidade e os três updates DOM. Eventos chegam ao alias proprietário como `OnDragonBoardPanelEvent`.

Registros Papyrus são apagados na troca de save. O consumer deve registrar em `OnInit` e novamente em `OnPlayerLoadGame`.

### Regras para consumers

- registrar durante/depois de SKSE post-load;
- usar um id global estável, normalmente `Autor.Mod.Painel`;
- manter callback e `userData` válidos até `UnregisterPanel`;
- não tocar no DOM diretamente pela game thread;
- tratar updates DOM como pedidos aceitos, não como confirmação de que o elemento existia;
- usar ids estáticos para controles interativos;
- consultar capabilities antes de usar qualquer superfície futura.

## Integrações opcionais

- HIGGS: interface obtida em `kPostPostLoad`; habilita grab direto de objetos/livros e dados de dedos. A ausência não impede o plugin de carregar.
- VRIK: o alias registra o gesto `DragonBoardVR_Toggle`; a integração local também envia poses de apontar/restaurar para toque por dedo.
- Spell Wheel VR: integração opcional via messaging/event bridge e INI de custom console command. A lógica principal de abertura continua dentro do DragonBoardVR.

## Renderização e performance

`RmlPresentBridge` troca a entrada 8 da vtable do swap chain para executar o render antes do `Present` original. `DragonBoardRmlRenderer` implementa shaders, buffers, texturas WIC, scissor e draw calls RmlUi. `D3D11StateGuard` captura/restaura o estado necessário para não contaminar a renderização do jogo.

`RmlRenderScheduler` renderiza somente quando há dirty reason (`Open`, `Document`, `Data`, `Pointer`, `Scroll`, `Animation`, `Resolution`) e respeita `iMaxActiveFPS`. Frames sem mudança reutilizam a textura anterior.

Resolução é configurável, mas deve manter 16:9. O layout lógico permanece 1920x1080 enquanto o render target pode ser menor. Inventory e Magic usam `RmlVirtualList` com overscan e pool máximo de dez linhas.

O state guard foi mantido e isolado porque o custo medido anteriormente era pequeno frente ao risco de corromper estado gráfico. Mudanças futuras devem começar por medição, depois reduzir renders, depois listas/DOM e somente então rever state D3D11.

## Ferramentas e verificação automatizada

- `DragonBoardRmlPreview`: usa o renderer de produção sem SKSE/CommonLib/game state.
- `RmlSourceEditor`: lista RML/RCSS, autosave, syntax highlighting, reload, último preview válido e navegação do elemento visual para fonte.
- `RmlVisualInspector`: inspeção visual do documento no preview.
- `RmlVirtualListTests`: teste standalone da janela virtual e mapeamento de índices.
- `CompilePapyrus.ps1`: compila o stub nativo `DragonBoardVR.psc`.
- `GenerateStatusScreen.ps1`: gera o NIF da superfície status com diffuse path próprio.

Na análise atual, o target `RmlVirtualListTests` foi recompilado a partir do source corrente e executado com sucesso. Não há CI no repositório e não foram encontrados testes automatizados equivalentes para lifecycle, API, settings/layout, input, equip, quests, surfaces ou integração SKSE.

## Build, saída e implantação

Comandos previstos:

```powershell
xmake build DragonBoardVR
xmake build DragonBoardRmlPreview
xmake build RmlVirtualListTests
```

O `after_build` copia DLL, RML/RCSS/assets, Papyrus, meshes, texturas e integração Spell Wheel para `install_output`.

Limites atuais do pacote do repositório:

- `install_output` não é limpo antes da cópia e contém arquivos que já não existem em `Assets`, portanto não é prova de um pacote reproduzível;
- o código referencia diversos NIFs DragonBoardVR que não estão em `Assets/meshes` deste checkout;
- não há ESP/ESM no repositório nem PEX compilado de `DragonBoardVR_PlayerAlias.psc` no pipeline mostrado;
- a versão do xmake é `1.0.0`, enquanto `Plugin::VERSION` é `1.1.3.0`;
- o manifest declara GPL-3.0, mas não há arquivo de licença no root.

Esses itens podem ser fornecidos por uma distribuição externa/legada, mas um build fresco deste repositório não demonstra isso. Consulte os itens de release/package em `TODO.md` antes de publicar.

## Invariantes para agentes

1. Hand-following não é world pinning. Não substitua o vínculo ao skeleton por um quad parado no mundo.
2. Preserve o preview 3D vivo ao trabalhar no Item Editor RML.
3. Trigger esquerdo e direito devem equipar e desequipar suas respectivas mãos.
4. Nunca acesse objetos do Skyrim por um listener RmlUi no `Present`.
5. Cada superfície independente precisa de context, render target, SRV, NIF e diffuse path próprios.
6. Não trate build, preview ou hash do `install_output` como validação in-game.
7. Para um teste runtime, confira DLL de build, `install_output` e mod ativo, encerre Skyrim, implante, compare hashes e reinicie.
8. Preserve alterações locais do usuário; o snapshot atual tem trabalho não commitado.
9. Otimização deve seguir evidência e mudanças reversíveis, sem misturar alteração visual/interação.
10. Métodos de surface da API v2 não são funcionalidade disponível enquanto a capability não for anunciada.

## Roteiro de investigação por tipo de tarefa

| Tarefa | Começar por |
| --- | --- |
| Plugin não carrega | `Src/main.cpp`, `Src/bootstrap/PluginLifecycle.cpp`, log SKSE e DLL implantado |
| Board não abre | `VRFrameUpdater.cpp` -> `InteractionInputController.cpp` -> `VRMenuManager::toggleMenu()` -> `MenuLifecycleController.cpp` |
| Board não acompanha a mão | `VRUIHandTracking.cpp`, `MenuPanelPresenter.cpp`, `VRUIPanel.cpp` |
| Troca de página incorreta | `VRMenuManager::switchToPanel()`, `PanelManagementController`, `RmlPanelHost::Open*()/Close()` |
| Click/scroll RML | `RmlInputBridge`, `DragonBoardRmlUi::ProcessInput`, bindings por id e `PointerInteractionController` |
| Inventário/magia | container em `Src/vrui`, presenter em `Src/ui/rml` e action dispatch em `RmlPanelHost` |
| Editor/pins | `VRUIItemEditPanel`, `RmlPanelHost`, `FixedWidgetPresenter`, `VRUILayoutManager` |
| Mapa/quest | `VRUIMapMarker`, `MapCalibration`, bloco Journal/Quest de `RmlPanelHost` |
| API externa | `DragonBoardVR_API.h/.cpp`, bloco external panel de `RmlPanelHost` e `PapyrusPanelBridge.cpp` |
| Performance | `RmlPresentBridge`, `RmlRenderScheduler`, `RmlPerformanceMetrics`, `DragonBoardRmlRenderer`, `D3D11StateGuard` |
| Assets/pacote | `xmake.lua`, `Assets/`, `Tools/GenerateStatusScreen.ps1` e árvore limpa de staging |

## Estado de validação conhecido

Confirmado por inspeção ou teste local não-game:

- target VR-only e dependências de build;
- API de página C++/Papyrus e stubs de surface v2;
- pipeline game thread/Present e render-on-dirty;
- virtualização de lista, com teste standalone aprovado;
- documentos e controles internos presentes no source;
- persistência INI/JSON e rotas de pin implementadas.

Ainda exige evidência in-game atual:

- transições Inventory/Magic para Journal/Settings sem segundo click ou preview residual;
- equip/desequip de duas cópias idênticas em ambas as mãos;
- restauração de quest/objective marker após save/load;
- todos os destinos de pin após fechar/reabrir e reiniciar o jogo;
- toque por dedo e pose VRIK no working tree atual;
- pacote completo produzido a partir de checkout limpo;
- comportamento amplo após as otimizações RmlUi.
