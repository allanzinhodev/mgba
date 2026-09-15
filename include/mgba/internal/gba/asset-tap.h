/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_ASSET_TAP_H
#define GBA_ASSET_TAP_H

#include <mgba-util/common.h>

CXX_GUARD_START

/*
 * ASSET TAP -- extracao de sprites com PROVENIENCIA.
 *
 * Feito para o Final Fantasy Tactics Advance, onde o problema nao e ver o
 * sprite na tela e sim descobrir ONDE ELE ESTA NA ROM. Serve a qualquer jogo
 * de GBA que comprima graficos com a BIOS, que e quase todos.
 *
 *
 * O PROBLEMA
 *
 * Procurar sprite por varredura estatica da ROM nao funciona num jogo que
 * comprime: os bytes na ROM nao se parecem com a imagem. Dava para despejar a
 * VRAM e recuperar a arte -- o hardware do GBA nao entende formato proprio,
 * entao o jogo e obrigado a converter para tile 4bpp e paleta RGB555 antes de
 * desenhar. Mas o despejo de VRAM responde "como e o sprite" e perde "de onde
 * ele veio", que e o que permite extrair os OUTROS em lote.
 *
 *
 * A IDEIA
 *
 * O elo perdido esta na chamada de BIOS. Quando o jogo descomprime, ele passa
 * o endereco de ORIGEM (na ROM) e o de DESTINO (VRAM/WRAM) em r0/r1. Essa
 * chamada e o unico ponto do sistema onde os dois lados aparecem juntos.
 *
 * O tap registra cada uma delas. Disparar a animacao de uma magia uma vez e o
 * bastante para saber o endereco na ROM do grafico dela -- e dali a extracao
 * estatica volta a ser possivel, agora sabendo onde procurar.
 *
 * Alem disso, a cada quadro o tap anota o estado da camada de objetos: os
 * tiles de OBJ, a paleta de OBJ e os 128 registros de OAM. Uma animacao vira
 * uma sequencia ordenada, e nao uma captura solta.
 *
 *
 * COMO LIGAR
 *
 *     set MGBA_ASSET_TAP=C:\caminho\de\saida
 *
 * Sem a variavel, tudo aqui e um `if` que da falso -- nenhum custo. A escolha
 * por variavel de ambiente, e nao por opcao de linha de comando, e para valer
 * em qualquer frontend (SDL, Qt, libretro) sem mexer no parser de argumentos
 * de cada um.
 */

struct GBA;

void GBAAssetTapInit(struct GBA* gba);
void GBAAssetTapDeinit(struct GBA* gba);

/** Uma descompressao de BIOS: kind e "lz77-wram", "lz77-vram", "huffman"... */
void GBAAssetTapDecompress(struct GBA* gba, const char* kind, uint32_t source, uint32_t dest, uint32_t size);

/**
 * Uma transferencia de DMA, registrada no inicio dela.
 *
 * So entra no log quando toca graficos: destino em VRAM, paleta ou OAM, ou
 * origem na ROM. O resto e som e logica de jogo, e encheria o arquivo.
 */
void GBAAssetTapDMA(struct GBA* gba, int channel, uint32_t source, uint32_t dest, uint32_t count, uint32_t width);

/** Fim de quadro: anota OAM, paleta e tiles de OBJ quando mudarem. */
void GBAAssetTapFrameEnded(struct GBA* gba);

CXX_GUARD_END

#endif
