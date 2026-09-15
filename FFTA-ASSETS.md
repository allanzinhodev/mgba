# Caça às sprites do Final Fantasy Tactics Advance

Este fork do mGBA tem um **asset tap**: instrumentação que grava, enquanto o
jogo roda, de onde os gráficos vieram e como a camada de objetos foi montada
a cada quadro.

Serve a qualquer jogo de GBA, mas foi feito para um problema específico do
FFTA — e a forma do problema é o que explica o desenho.

## O problema

Varredura estática da ROM não acha sprite num jogo que comprime: os bytes na
ROM não se parecem com a imagem. Despejar a VRAM resolve *como é* o sprite,
porque o hardware do GBA não entende formato próprio e obriga o jogo a
converter para tile 4bpp e paleta BGR555 antes de desenhar — mas perde *de
onde ele veio*, que é o que permite extrair os outros em lote.

O elo que faltava é a **proveniência**: ligar o que apareceu na tela ao
endereço na ROM.

## Como usar

```
set MGBA_ASSET_TAP=C:\saida
build\mgba-sdl.exe -C audioSync=0 -C mute=1 caminho\do\jogo.gba
```

Jogue até a animação que interessa. Depois:

```
node tools\ffta-sprites\compose.js C:\saida
```

Os PNGs saem em `C:\saida\png\`, um por quadro, na ordem — a sequência de
arquivos **é** a animação.

> **`-C audioSync=0` não é detalhe.** O mGBA sincroniza a emulação com o
> áudio; numa máquina sem dispositivo de saída, a emulação **não avança** e a
> janela fica preta. Isso custou três sessões de captura até o sinal de vida
> (abaixo) apontar a causa.

## O que é gravado

| arquivo | conteúdo |
|---|---|
| `decompress.jsonl` | cada DMA que toca gráfico e cada descompressão de BIOS, com origem e destino |
| `frames.jsonl` | um registro por **mudança** na camada de objetos, com os 128 registros de OAM decodificados |
| `tiles/` `palettes/` `oam/` | os despejos, nomeados pelo hash do conteúdo |

Três decisões que valem conhecer antes de mexer:

**Nome pelo hash do conteúdo.** Uma animação reusa a mesma paleta em dezenas
de quadros; nomear pelo conteúdo deduplica de graça, e vale **entre
execuções** — capturar outra magia amanhã não regrava o que hoje já saiu.

**Quadro sem mudança não vira registro.** Sem isso, cinco minutos de jogo
geram 18 mil linhas quase idênticas, e achar a animação no meio vira o novo
problema.

**Sinal de vida a cada 600 quadros**, mesmo sem mudança. Sem ele não dá para
distinguir "o emulador não avançou" de "avançou e nada mudou" — os dois
produzem um arquivo de uma linha só.

## O que a instrumentação já descobriu sobre o FFTA

**O jogo não usa a descompressão da BIOS.** Uma sessão de 21 mil quadros não
gerou uma linha sequer pelo hook de BIOS: o FFTA traz o próprio
descompressor e nunca chama `SWI 0x11/0x12`. O hook continua no código
porque vale para a maioria dos jogos de GBA, mas neste está mudo.

**Os tiles de OBJ vêm direto da ROM, sem compressão.** Na mesma sessão:

```
0x0897D078 -> 0x06010000, 32768 bytes
```

32 KB indo da ROM para a área de tiles de objeto. Isso explica por que a
busca por blocos comprimidos não achava os sprites — eles não estão
comprimidos.

**Provado**: os 32 KB lidos da ROM em `0x0897D078` são byte a byte idênticos
ao despejo que o tap gravou pela VRAM. Os dois caminhos, estático e
dinâmico, chegam ao mesmo lugar.

### Quando a origem não for a ROM

Nem todo gráfico virá direto do cartucho: quem descomprime primeiro e copia
depois aparece como `IWRAM -> VRAM` ou `EWRAM -> VRAM`. O log ainda serve,
porque o DMA que **encheu** aquela região também está registrado — a cadeia
se remonta lendo o arquivo de trás para frente.

## Onde está o código

| arquivo | papel |
|---|---|
| `src/gba/asset-tap.c` | a instrumentação |
| `include/mgba/internal/gba/asset-tap.h` | a interface, com o raciocínio |
| `src/gba/dma.c` | o hook de DMA, no início da transferência |
| `src/gba/bios.c` | o hook de descompressão da BIOS |
| `src/gba/gba.c` | init, deinit e o hook de fim de quadro |
| `tools/ffta-sprites/compose.js` | monta os PNGs a partir do despejo |

Sem `MGBA_ASSET_TAP` definida, tudo isso é um `if` que dá falso.

## Build

```
powershell -ExecutionPolicy Bypass -File build-windows.ps1
```

Monta um mGBA reduzido — sem Qt, FFmpeg, libzip, sqlite3 e LZMA, que não
servem para extrair. Precisa de `VCPKG_ROOT` apontando para um vcpkg com
`sdl2`, `libpng`, `zlib` e `libepoxy` no triplet `x64-windows`.

O triplet é o dinâmico **por imposição**: `libepoxy` não suporta CRT estática
no Windows, e o epoxy não é opcional — o `CMakeLists` tem um `FATAL_ERROR`
que nem `USE_EPOXY=OFF` contorna. Por isso o release leva as DLLs junto.
