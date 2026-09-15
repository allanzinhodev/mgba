/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/asset-tap.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/video.h>

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define ASSET_TAP_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define ASSET_TAP_MKDIR(p) mkdir((p), 0755)
#endif

/*
 * Onde ficam os tiles de OBJ na VRAM.
 *
 * Nos modos de tile (0-2) a area de objetos comeca em 0x06010000 e tem 32 KB.
 * Nos modos de bitmap (3-5) so a metade de cima e utilizavel, porque o
 * framebuffer ocupa o resto -- despejamos os 32 KB do mesmo jeito, e cabe a
 * quem le decidir. Custa 32 KB por estado UNICO, nao por quadro.
 */
#define OBJ_TILES_OFFSET 0x10000
#define OBJ_TILES_SIZE 0x8000

/* A metade de OBJ da palette RAM: 256 cores, 16 subpaletas de 16. */
#define OBJ_PALETTE_INDEX 256
#define OBJ_PALETTE_ENTRIES 256

static struct {
	bool active;
	char dir[512];
	FILE* decompress;
	FILE* frames;
	uint64_t lastTiles;
	uint64_t lastPalette;
	uint64_t lastOAM;
} s_tap;

/*
 * FNV-1a de 64 bits.
 *
 * Serve para dois propositos: detectar que o estado MUDOU desde o quadro
 * anterior, e dar nome ao arquivo do despejo. Nomear pelo conteudo faz a
 * deduplicacao sair de graca -- uma animacao reusa a mesma paleta em dezenas
 * de quadros, e todos apontam para o mesmo arquivo.
 *
 * Nao e criptografico e nao precisa ser: um choque faria dois estados
 * distintos virarem um so, e o risco disso em algumas centenas de despejos e
 * desprezivel perto do custo de carregar um SHA aqui dentro.
 */
static uint64_t _hash(const void* data, size_t size) {
	const uint8_t* bytes = data;
	uint64_t h = 0xCBF29CE484222325ULL;
	size_t i;
	for (i = 0; i < size; ++i) {
		h ^= bytes[i];
		h *= 0x100000001B3ULL;
	}
	return h;
}

static void _path(char* out, size_t outSize, const char* sub, const char* name) {
	if (name) {
		snprintf(out, outSize, "%s/%s/%s", s_tap.dir, sub, name);
	} else {
		snprintf(out, outSize, "%s/%s", s_tap.dir, sub);
	}
}

/*
 * Grava o blob so se ele ainda nao existir.
 *
 * O teste de existencia e o proprio sistema de arquivos, em vez de uma tabela
 * em memoria. Isso mantem a deduplicacao valida ENTRE EXECUCOES: rodar o
 * emulador de novo para capturar outra magia nao regrava o que a primeira
 * sessao ja tinha extraido.
 */
static void _writeBlobOnce(const char* sub, uint64_t hash, const void* data, size_t size) {
	char name[64];
	char full[640];
	snprintf(name, sizeof(name), "%016llx.bin", (unsigned long long) hash);
	_path(full, sizeof(full), sub, name);

	FILE* probe = fopen(full, "rb");
	if (probe) {
		fclose(probe);
		return;
	}

	FILE* f = fopen(full, "wb");
	if (!f) {
		mLOG(GBA, WARN, "asset tap: nao consegui gravar %s", full);
		return;
	}
	fwrite(data, 1, size, f);
	fclose(f);
}

void GBAAssetTapInit(struct GBA* gba) {
	UNUSED(gba);
	memset(&s_tap, 0, sizeof(s_tap));

	const char* dir = getenv("MGBA_ASSET_TAP");
	if (!dir || !dir[0]) {
		return;
	}

	strncpy(s_tap.dir, dir, sizeof(s_tap.dir) - 1);
	ASSET_TAP_MKDIR(s_tap.dir);

	char sub[640];
	_path(sub, sizeof(sub), "tiles", NULL);
	ASSET_TAP_MKDIR(sub);
	_path(sub, sizeof(sub), "palettes", NULL);
	ASSET_TAP_MKDIR(sub);
	_path(sub, sizeof(sub), "oam", NULL);
	ASSET_TAP_MKDIR(sub);

	char full[640];
	_path(full, sizeof(full), "decompress.jsonl", NULL);
	s_tap.decompress = fopen(full, "wb");
	_path(full, sizeof(full), "frames.jsonl", NULL);
	s_tap.frames = fopen(full, "wb");

	if (!s_tap.decompress || !s_tap.frames) {
		mLOG(GBA, ERROR, "asset tap: nao consegui abrir os arquivos de saida em %s", s_tap.dir);
		GBAAssetTapDeinit(gba);
		return;
	}

	s_tap.active = true;
	mLOG(GBA, INFO, "asset tap ligado, gravando em %s", s_tap.dir);
}

void GBAAssetTapDeinit(struct GBA* gba) {
	UNUSED(gba);
	if (s_tap.decompress) {
		fclose(s_tap.decompress);
	}
	if (s_tap.frames) {
		fclose(s_tap.frames);
	}
	memset(&s_tap, 0, sizeof(s_tap));
}

void GBAAssetTapDecompress(struct GBA* gba, const char* kind, uint32_t source, uint32_t dest, uint32_t size) {
	if (!s_tap.active) {
		return;
	}
	/*
	 * ESTA E A LINHA QUE INTERESSA.
	 *
	 * `source` aponta para a ROM e `dest` para onde o grafico vai. Um registro
	 * destes por animacao disparada e o suficiente para localizar o asset no
	 * cartucho -- que era exatamente o que faltava na extracao estatica.
	 */
	fprintf(s_tap.decompress,
	        "{\"frame\":%u,\"kind\":\"%s\",\"src\":\"0x%08X\",\"dest\":\"0x%08X\",\"size\":%u}\n",
	        gba->video.frameCounter, kind, source, dest, size);
	fflush(s_tap.decompress);
}

void GBAAssetTapFrameEnded(struct GBA* gba) {
	if (!s_tap.active) {
		return;
	}

	struct GBAVideo* video = &gba->video;
	const uint8_t* tiles = (const uint8_t*) video->vram + OBJ_TILES_OFFSET;
	const uint16_t* palette = &video->palette[OBJ_PALETTE_INDEX];
	const uint16_t* oam = video->oam.raw;

	/*
	 * SINAL DE VIDA.
	 *
	 * Sem isto nao da para distinguir "o emulador nao avancou" de "avancou e
	 * nada mudou na camada de objetos" -- os dois produzem o mesmo arquivo de
	 * uma linha so, e a primeira sessao de captura foi gasta justamente nessa
	 * duvida. Uma linha a cada 600 quadros (10 segundos) responde de graca.
	 */
	if (video->frameCounter % 600 == 0) {
		fprintf(s_tap.frames, "{\"frame\":%u,\"heartbeat\":true}\n", video->frameCounter);
		fflush(s_tap.frames);
	}

	uint64_t hTiles = _hash(tiles, OBJ_TILES_SIZE);
	uint64_t hPalette = _hash(palette, OBJ_PALETTE_ENTRIES * sizeof(uint16_t));
	uint64_t hOAM = _hash(oam, GBA_SIZE_OAM);

	/*
	 * Quadro em que nada mudou nao vira registro.
	 *
	 * Sem isto uma sessao de cinco minutos gera 18 mil linhas, quase todas
	 * iguais, e achar a animacao no meio vira o novo problema. Com isto, cada
	 * linha e uma MUDANCA -- e a lista de linhas ja e a lista de quadros
	 * distintos da animacao.
	 */
	if (hTiles == s_tap.lastTiles && hPalette == s_tap.lastPalette && hOAM == s_tap.lastOAM) {
		return;
	}
	s_tap.lastTiles = hTiles;
	s_tap.lastPalette = hPalette;
	s_tap.lastOAM = hOAM;

	_writeBlobOnce("tiles", hTiles, tiles, OBJ_TILES_SIZE);
	_writeBlobOnce("palettes", hPalette, palette, OBJ_PALETTE_ENTRIES * sizeof(uint16_t));
	_writeBlobOnce("oam", hOAM, oam, GBA_SIZE_OAM);

	uint16_t dispcnt = gba->memory.io[GBA_REG(DISPCNT)];

	fprintf(s_tap.frames,
	        "{\"frame\":%u,\"mode\":%u,\"objMapping1D\":%s,\"tiles\":\"%016llx\",\"palette\":\"%016llx\",\"oam\":\"%016llx\",\"objs\":[",
	        video->frameCounter, dispcnt & 7, (dispcnt & 0x40) ? "true" : "false",
	        (unsigned long long) hTiles, (unsigned long long) hPalette, (unsigned long long) hOAM);

	int i;
	bool first = true;
	for (i = 0; i < 128; ++i) {
		struct GBAObj* obj = &video->oam.obj[i];
		/*
		 * Objeto desabilitado nao entra na lista.
		 *
		 * O bit 9 de A tem dois sentidos: em objeto normal e "desabilitado",
		 * em objeto transformado e "tamanho dobrado". Por isso o teste de
		 * Transformed vem antes -- ler o bit sem ele descartaria justamente os
		 * objetos afins, que sao os mais usados em animacao de magia.
		 */
		if (!GBAObjAttributesAIsTransformed(obj->a) && GBAObjAttributesAIsDisable(obj->a)) {
			continue;
		}
		fprintf(s_tap.frames,
		        "%s{\"i\":%d,\"x\":%u,\"y\":%u,\"tile\":%u,\"pal\":%u,\"shape\":%u,\"size\":%u,"
		        "\"c256\":%s,\"hflip\":%s,\"vflip\":%s,\"affine\":%s}",
		        first ? "" : ",", i,
		        GBAObjAttributesBGetX(obj->b), GBAObjAttributesAGetY(obj->a),
		        GBAObjAttributesCGetTile(obj->c), GBAObjAttributesCGetPalette(obj->c),
		        GBAObjAttributesAGetShape(obj->a), GBAObjAttributesBGetSize(obj->b),
		        GBAObjAttributesAIs256Color(obj->a) ? "true" : "false",
		        GBAObjAttributesBIsHFlip(obj->b) ? "true" : "false",
		        GBAObjAttributesBIsVFlip(obj->b) ? "true" : "false",
		        GBAObjAttributesAIsTransformed(obj->a) ? "true" : "false");
		first = false;
	}

	fprintf(s_tap.frames, "]}\n");
	fflush(s_tap.frames);
}
