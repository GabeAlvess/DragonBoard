# DragonBoardVR — TODO priorizado

## Como usar este arquivo

Este backlog foi derivado do working tree analisado em 2026-07-20, branch `codex/finger-touch-diagnostic`, commit `73a630d`, incluindo alterações locais não commitadas.

Prioridades:

- P0: bloqueia uma baseline confiável, teste de comportamento ou release reproduzível.
- P1: necessário para cumprir os pilares públicos do mod e entregar uma API robusta.
- P2: qualidade, amplitude e experiência depois dos contratos centrais estarem estáveis.

Estados recomendados para futuras atualizações: `TODO`, `EM ANDAMENTO`, `BLOQUEADO`, `VALIDAÇÃO IN-GAME`, `CONCLUÍDO`. Não marcar como concluído apenas porque compilou, abriu no preview ou gerou DLL.

Cada item inclui uma definição de pronto. O próximo documento de arquitetura deverá decidir a estrutura técnica usada para executar este backlog; este arquivo não antecipa essa decisão.

## P0 — baseline e release confiáveis

### P0-01 — Preservar e classificar o working tree atual

- [ ] Criar um snapshot não destrutivo dos arquivos modificados e não rastreados.
- [ ] Separar em commits reversíveis: finger touch/diagnóstico, segurança de equip, Spell Wheel/VRIK e demais mudanças.
- [ ] Registrar quais alterações possuem confirmação in-game e quais são somente experimento.
- [ ] Excluir `Backups/` e árvores externas de qualquer commit acidental.

Evidência atual: há 27 arquivos modificados, `Src/diagnostics/`, `Src/integrations/vrik/` e `FingerTouchController.*` não rastreados.

Pronto quando: o branch puder ser reconstruído a partir de commits com escopo claro, sem perder trabalho local e sem depender de arquivos soltos.

### P0-02 — Tornar o pacote de release determinístico

- [ ] Produzir o pacote em uma staging directory limpa, em vez de acumular arquivos em `install_output`.
- [ ] Gerar um manifest com todos os arquivos esperados e falhar se houver arquivo ausente ou inesperado.
- [ ] Eliminar duplicação entre `add_installfiles` e `after_build` ou definir uma única fonte de verdade.
- [ ] Comparar hashes do build, staging e mod ativo do MO2.
- [ ] Documentar o procedimento de deploy com Skyrim completamente encerrado.

Evidência atual: o build não limpa arquivos inesperados acumulados anteriormente em `install_output`; o pacote precisa ser produzido em staging limpa.

Pronto quando: dois builds de um checkout limpo produzirem a mesma lista de arquivos e o mesmo conteúdo, exceto metadados explicitamente não determinísticos.

### P0-03 — Fechar o inventário de assets e plugin data

- [ ] Listar todos os NIF/DDS/font/script/ESP usados pelo runtime e indicar sua origem.
- [ ] Adicionar ao repositório ou declarar como dependência cada mesh DragonBoardVR referenciada pelo código.
- [ ] Confirmar a origem de `IconPlane.nif`, `Tablet.nif`, `Player.nif`, `QuestMarker.nif`, `Unknow.nif`, icons, slots, laser, font symbols e `isEquipped.nif`.
- [ ] Incluir ou documentar o ESP/ESM que cria quest/alias e propriedades Papyrus necessárias.
- [ ] Compilar e empacotar `DragonBoardVR_PlayerAlias.pex` se ele fizer parte da instalação final.
- [ ] Validar todos os caminhos internos de textura dos NIFs e evitar nomes genéricos que substituam assets do Skyrim.

Evidência atual: `Assets/meshes` contém apenas os meshes diretamente mantidos pelo pacote, incluindo `dragonboard.nif` e `RmlUIScreen.nif`, enquanto o source referencia dezenas de meshes DragonBoardVR. O pipeline só instala `DragonBoardVR.pex`.

Pronto quando: uma instalação feita somente a partir do pacote gerado pelo repositório carregue todos os elementos esperados sem depender de restos de uma versão anterior.

### P0-04 — Unificar versão, licença e metadados de release

- [ ] Escolher uma única fonte de versão e gerar xmake, plugin metadata, log, API e documentação a partir dela.
- [ ] Resolver a divergência `set_version('1.0.0')` versus `Plugin::VERSION 1.1.3.0`.
- [x] Adicionar o arquivo `LICENSE` com a licença MIT.
- [ ] Adicionar README de instalação, requisitos, incompatibilidades e remoção segura.
- [ ] Criar changelog/release notes e versionar a API separadamente da versão do mod.

Pronto quando: DLL, package, log e documentação exibirem versões coerentes e o pacote tiver licença explícita.

### P0-05 — Estabelecer uma matriz mínima de regressão in-game

- [ ] Testar abertura/fechamento repetido com todos os modos de ativação usados pelo release.
- [ ] Testar modo de menu na mão esquerda e direita.
- [ ] Abrir Home, Map, Inventory, Magic, Journal, Settings, Developer, Mods e Item Editor.
- [ ] Testar fechamento pelo Board, pelo documento RML e por integração externa.
- [ ] Testar com/sem HIGGS, VRIK e Spell Wheel.
- [ ] Testar normal, combate, loading, new game, load game, troca de cell e game restart.
- [ ] Registrar log, vídeo curto, hashes dos DLLs e INI/layout usados.

Pronto quando: existe um checklist repetível com resultado por cenário e uma falha produz dados suficientes para localizar o caminho real.

### P0-06 — Corrigir a lifecycle seam entre páginas

- [ ] Reproduzir Inventory/Magic -> Journal e Inventory/Magic -> Settings.
- [ ] Rastrear `VRMenuManager::switchToPanel`, `PanelManagementController::SwitchTo`, `RmlPanelHost::Open*()/Close()` e `VRUIItemEditPanel::setRmlPreviewMode`.
- [ ] Garantir que a primeira ação fecha o documento anterior, abre o novo e remove preview residual.
- [ ] Testar a volta ao Home e a reabertura do painel anterior.

Evidência histórica: a transição já exigiu um segundo click e deixou o preview 3D visível; não há confirmação de correção definitiva.

Pronto quando: todas as transições da matriz de páginas funcionarem com um único click e com visibilidade/preview coerentes.

### P0-07 — Validar equip/desequip por mão e cópias idênticas

- [ ] Equipar duas cópias da mesma arma, uma em cada mão.
- [ ] Desequipar a esquerda somente com trigger esquerdo e a direita somente com trigger direito.
- [ ] Repetir com arma + spell, lights, stacks com e sem `ExtraDataList` e left-handed menu mode.
- [ ] Confirmar refresh visual no Inventory, Magic e pins.
- [ ] Confirmar que a ponte de skeleton não causa arma invisível, pose quebrada ou crash.

Evidência atual: o source diferencia `kWornLeft`/`kWorn` e possui fallback para a instância base, mas a validação manual completa não está registrada.

Pronto quando: os cenários de ambas as mãos passarem no jogo e o log identificar form, instância e slot selecionados.

### P0-08 — Validar persistência de pins, item editor e quest marker

- [ ] Editar position/rotation/scale de item, aplicar por item, trocar categoria e reabrir.
- [ ] Criar pins no Board, mão e mundo; fechar/reabrir o Board e reiniciar o Skyrim.
- [ ] Testar label hidden, repin de layout antigo e `visualTransformComposed`.
- [ ] Salvar/carregar com uma quest e objetivo rastreados; confirmar seleção e marker restaurados.
- [ ] Testar quest com alvo móvel, interior/exterior e objective progression.
- [ ] Confirmar que o custom quest marker não altera o marker nativo do Skyrim.

Pronto quando: INI/layout e save/load reproduzirem exatamente a pose/seleção visível antes do restart.

### P0-09 — Concluir ou isolar o experimento de finger touch

- [ ] Definir o contrato de toque: hover, press, release, scroll, retirada obrigatória e prioridade sobre o laser.
- [ ] Confirmar nós de dedo em skeleton vanilla e VRIK, mão esquerda/direita e handedness.
- [ ] Testar painéis RML, botões 3D, sliders, listas e preview sem click fantasma.
- [ ] Restaurar pose VRIK em close, pre-load, disable e erro.
- [ ] Medir custo do probe/markers e garantir que debug esteja desligado por padrão.
- [ ] Versionar a feature em commits separados ou removê-la da baseline até estar pronta.

Pronto quando: o modo puder ser ativado/desativado sem alterar o contrato do laser e tiver uma matriz in-game própria.

## P1 — API pública e extensibilidade

### P1-01 — Implementar o registry público de superfícies independentes

- [ ] Implementar `CreateSurface`, `DestroySurface`, `BindPanelToSurface`, visibilidade e transform.
- [ ] Anunciar capabilities somente depois da implementação e testes.
- [ ] Dar a cada superfície context, texture/RTV/SRV, scene node, diffuse path e lifetime próprios.
- [ ] Suportar anchors `DragonBoard`, `LeftHand`, `RightHand`, `Hmd` e `World` com semântica documentada.
- [ ] Implementar render-on-dirty, pointer, grab com uma mão, scale com duas mãos e persistência.
- [ ] Definir quotas/limites de resolução, FPS, número de superfícies e memória de GPU.
- [ ] Garantir coexistência de múltiplas superfícies e da página principal.

Dependência: P0-02/P0-03, porque cada superfície precisa de asset/texture binding reproduzível.

Pronto quando: um plugin exemplo externo criar dois widgets simultâneos, manipulá-los no jogo, persistir transforms e destruí-los sem vazamento ou textura cruzada.

### P1-02 — Formalizar os dois tipos de painel do usuário

- [ ] Definir descriptor/manifest para `PagePanel` e `WidgetSurface`.
- [ ] Permitir descoberta de painéis instalados sem exigir que todo autor escreva C++.
- [ ] Definir id, documento, stylesheet/assets, tipo, anchor, tamanho lógico/físico, FPS, flags e versão mínima da API.
- [ ] Validar paths e mostrar erro de load legível no jogo/log.
- [ ] Definir reload seguro durante desenvolvimento e comportamento em produção.

Pronto quando: um usuário instalar um pacote declarativo de exemplo para cada tipo e o DragonBoard carregá-los sem patch no código principal.

### P1-03 — Entregar API de botões do Board

- [ ] Adicionar registro/remoção de botão por owner/id.
- [ ] Permitir associar diretamente um botão a um `PanelHandle` ou action registrada.
- [ ] Expor label, icon/NIF, tooltip, posição inicial, scale, ordem/container e handedness.
- [ ] Persistir transform do usuário sem permitir que uma atualização do mod apague a customização.
- [ ] Definir conflitos de id, botão duplicado, mod ausente e painel indisponível.
- [ ] Oferecer equivalente Papyrus e/ou manifest declarativo.

Evidência atual: os botões fixos e oito slots são internos; não existe `RegisterButton` na API C++/Papyrus.

Pronto quando: um mod externo registrar um botão que abre sua página no primeiro click, sobreviver a restart e ser removido quando o owner não existir.

### P1-04 — Ampliar lifecycle e diagnóstico da API

- [ ] Adicionar eventos de `Queued`, `Ready`, `Visible`, `Hidden`, `Failed` e `Destroyed`.
- [ ] Expor código e mensagem do último erro de load/DOM/surface.
- [ ] Separar “pedido aceito” de “operação aplicada”.
- [ ] Definir comportamento de show antes de ready, show concorrente e unregister durante callback.
- [ ] Garantir reentrância segura e documentar o domínio de thread de cada callback.
- [ ] Definir cleanup automático por owner/plugin e proteção contra callback/userData inválido.

Pronto quando: um consumer consegue diagnosticar sem ler o log por que seu painel não apareceu e não recebe callback depois do unregister.

### P1-05 — Oferecer data binding e DOM dinâmico seguro

- [ ] Adicionar operações em lote para reduzir filas e renders.
- [ ] Suportar listas/datasets sem o consumer concatenar markup.
- [ ] Registrar/desregistrar interactives criados depois do load.
- [ ] Expor focus, enabled/disabled, value e seleção de forma tipada.
- [ ] Definir escaping e limites de payload.
- [ ] Tornar updates transacionais quando vários elementos formam um único estado visual.

Evidência atual: a API só altera text, attribute e class; interactives dinâmicos não entram automaticamente no fallback VR.

Pronto quando: um exemplo externo atualiza uma lista dinâmica, adiciona controles interativos e recebe eventos com um único commit de UI.

### P1-06 — Dar paridade adequada ao Papyrus

- [ ] Expor versão/capabilities e panel state em Papyrus.
- [ ] Definir API de widget/button compatível com os limites do VM.
- [ ] Automatizar reregistro após load ou fornecer um lifecycle event explícito.
- [ ] Compilar e testar PEXs no pipeline.
- [ ] Criar exemplo completo com quest/alias e evento.

Pronto quando: um mod Papyrus-only implementar uma página e um botão/widget suportado sem depender de C++ auxiliar.

### P1-07 — Publicar um SDK versionado

- [ ] Extrair header público sem dependências desnecessárias e documentar compatibilidade ABI.
- [ ] Definir negociação de versão futura sem aumentar uma vtable de forma incompatível.
- [ ] Criar sample consumer C++ compilável e sample Papyrus instalável.
- [ ] Adicionar changelog de API, deprecations e política de suporte.
- [ ] Testar consumer antigo contra DLL nova e consumer novo contra DLL antiga.

Pronto quando: a API for consumida a partir de um pacote SDK, não pela cópia informal de um header do source.

### P1-08 — Formalizar actions e limites de segurança

- [ ] Transformar console/cast/equip em actions tipadas e registráveis.
- [ ] Definir quais ações exigem confirmação, fade ou são proibidas.
- [ ] Validar FormIDs estáveis por plugin/local id, não apenas runtime FormID serializado.
- [ ] Escapar/validar o formato INI de label, icon e command.
- [ ] Substituir `ModActionManager::loadActions()` vazio por implementação real ou remover a API morta.
- [ ] Tornar o salvamento assíncrono encerrável e observável, sem thread detached fora do lifetime do serviço.

Pronto quando: actions inválidas falharem com diagnóstico, updates não perderem dados e um owner não conseguir acionar comportamento não declarado por engano.

## P1 — personalização como contrato de produto

### P1-09 — Expor todos os transforms relevantes no jogo

- [ ] Inventariar cada elemento que pode ou deve aceitar position/rotation/scale.
- [ ] Garantir edição consistente para Board, página RML, widget, botão, preview, pin, marker e label.
- [ ] Mostrar valores atuais, reset por elemento e reset global com confirmação.
- [ ] Preservar overrides do usuário em updates e migrações de schema.
- [ ] Definir bounds e snapping por tipo sem impedir liberdade avançada.

Pronto quando: a matriz de elementos personalizáveis tiver editor, persistência, reset e teste de restart.

### P1-10 — Versionar e proteger INI/layout do usuário

- [ ] Adicionar `schemaVersion` ao JSON e versão de formato aos INIs relevantes.
- [ ] Fazer writes atômicos com arquivo temporário e replace seguro.
- [ ] Manter backup recuperável antes de migração.
- [ ] Validar números finitos, matrizes, scale, ids e strings antes de aplicar.
- [ ] Resolver conflito entre INI, JSON e defaults com precedência documentada.
- [ ] Não salvar o arquivo inteiro por cada micro movimento; agrupar no release da interação.

Pronto quando: layouts antigos migrarem sem perder pose e um arquivo truncado/corrompido recuperar defaults ou backup sem crash.

### P1-11 — Completar Settings para o conjunto real de opções

- [ ] Mapear opções existentes no INI que ainda não aparecem no painel RML.
- [ ] Organizar por interação, visual, performance, pins, mapa, integrações e debug.
- [ ] Separar opções avançadas/perigosas e mostrar necessidade de restart quando aplicável.
- [ ] Testar hot reload sem rebuild total desnecessário.

Pronto quando: o usuário puder configurar o comportamento normal do mod no Board sem editar texto manualmente, mantendo edição manual para casos avançados.

## P1 — qualidade e observabilidade

### P1-12 — Expandir testes automatizados

- [ ] Testar `ActionExecutor` parse/danger classification.
- [ ] Testar `CooldownTimer`, `DeferredTaskQueue`, hold/press trackers e render scheduler.
- [ ] Testar settings migration, item stable keys e layout JSON round trip.
- [ ] Testar presenters, busca/filtro/seleção e mapeamento de índices.
- [ ] Testar fila/state machine da API externa e comandos DOM.
- [ ] Testar map calibration e seleção de quest com modelos puros.
- [ ] Criar mocks/adapters para equip hand semantics onde CommonLib não puder rodar offline.
- [ ] Adicionar teste que carrega todos os RML/RCSS e verifica ids obrigatórios.

Pronto quando: regressões de lógica principal falharem fora do jogo antes do deploy.

### P1-13 — Adicionar CI e quality gates

- [ ] Build releasedbg/release do plugin, preview, tests e Papyrus.
- [ ] Rodar testes, `git diff --check`, análise estática e validação de package manifest.
- [ ] Verificar versão, licença, PEX atualizado, assets referenciados e RML carregável.
- [ ] Publicar artefato de staging e checksums.

Pronto quando: um commit não puder ser considerado release candidate se build, testes ou manifest falharem.

### P1-14 — Endurecer o hook de Present e lifetime D3D11

- [ ] Detectar troca/recriação de swap chain e device lost.
- [ ] Definir coexistência com outros hooks da vtable e preservar a chain.
- [ ] Garantir shutdown/release ordenado de contexts, textures, SRVs e scene bridges.
- [ ] Testar fullscreen transitions, loading, save load e fechamento do processo.
- [ ] Manter o state guard até nova medição demonstrar alternativa segura.

Pronto quando: o renderer se recuperar de recriação de recursos sem textura preta, crash ou contaminação do jogo.

### P1-15 — Padronizar logs e pacote de diagnóstico

- [ ] Separar níveis startup, lifecycle, input, API, render, gameplay e performance.
- [ ] Evitar logs `info` por evento/frame no modo normal.
- [ ] Adicionar ids de painel/surface/action e estado anterior/novo às transições.
- [ ] Criar comando para exportar versão, capabilities, settings relevantes, hashes e últimos erros.
- [ ] Redigir roteiro de coleta de log para usuários.

Pronto quando: uma falha de abertura, input, textura ou API puder ser classificada sem recompilar com logs improvisados.

## P2 — experiência, conteúdo e manutenção

### P2-01 — Tornar o editor RML uma ferramenta de autoria completa

- [ ] Validar todos os documentos internos no preview.
- [ ] Adicionar templates de Page e Widget alinhados à API/manifest finais.
- [ ] Mostrar erros de parse, recursos ausentes e ids duplicados no editor.
- [ ] Simular pointer, trigger, grip scroll, handedness e callbacks.
- [ ] Exportar pacote instalável de exemplo.

### P2-02 — Temas, localização e legibilidade VR

- [ ] Separar tokens visuais de layout funcional.
- [ ] Oferecer tema, fonte, contraste, escala de texto e cursor configuráveis.
- [ ] Remover strings hardcoded e definir localization contract para painéis internos/externos.
- [ ] Testar tamanhos de texto e scroll em HMDs/resoluções diferentes.

### P2-03 — Generalizar widgets internos úteis

- [ ] Migrar Status para o registry público quando P1-01 estiver pronto.
- [ ] Definir widgets opcionais de performance, quest, recursos do player e quick actions.
- [ ] Permitir múltiplas instâncias somente quando lifetime e quotas estiverem definidos.

### P2-04 — Ampliar testes de escala e performance

- [ ] Criar datasets sintéticos para 25/250/1000 itens completos no preview.
- [ ] Medir average/p95/p99 de Present, Rml update/render, DOM e draw calls.
- [ ] Testar múltiplas superfícies e widgets com FPS distintos.
- [ ] Definir budgets de CPU/GPU/memória e critérios de regressão.
- [ ] Otimizar nesta ordem: medir, reduzir renderizações, reduzir/virtualizar DOM/listas, só então rever estado D3D11.

### P2-05 — Limpar compatibilidade e arquivos históricos

- [ ] Classificar `.bak`, `stable_*`, `design-preview`, `nif_tests` e external sources.
- [ ] Mover backups para armazenamento fora do source de release sem apagá-los inadvertidamente.
- [ ] Documentar dependências vendored/symlink e como restaurá-las.
- [ ] Remover caminhos de fallback somente depois de confirmar que não são usados por instalações existentes.

## Documento de arquitetura

### NEXT-01 — `CONCLUÍDO` — Criar `ARCHITECTURE.md`

`ARCHITECTURE.md` foi criado a partir do snapshot de 2026-07-20 e contém:

- arquitetura atual com ownership e dependências entre módulos;
- sequência de lifecycle e state machines de menu/painel/surface;
- modelo de threads e filas;
- limites entre engine adapters, domínio, UI 3D, RmlUi e API pública;
- análise dos monólitos (`RmlPanelHost`, `DragonBoardRmlUi`, `VRUIButton`, containers e settings);
- estratégia de migração incremental, mantendo comportamento e commits reversíveis;
- proposta de registry unificado para Page/Widget/Button/Action;
- modelo de persistência versionado;
- estratégia de testes e deployment;
- decisões, alternativas, riscos e ordem de execução.

O documento recomenda uma migração incremental. Não iniciar uma reescrita ampla: as primeiras mudanças devem atacar ownership e seams comprovadas, preservando a experiência VR e as customizações existentes.

## Checklist curto antes de declarar uma tarefa concluída

- [ ] O caminho real de código foi identificado.
- [ ] Alterações locais não relacionadas foram preservadas.
- [ ] O projeto/target relevante compilou.
- [ ] Testes offline relevantes passaram.
- [ ] Build, staging e DLL ativa foram comparados quando aplicável.
- [ ] Skyrim foi reiniciado após trocar DLL/assets nativos.
- [ ] O comportamento foi testado nas duas mãos quando envolve input/equip.
- [ ] Close/reopen e game restart foram testados quando envolve persistência.
- [ ] Logs não mostram erro novo.
- [ ] Documentação/API/TODO foram atualizados conforme o resultado real.
