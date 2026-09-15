'use strict';
/*
 * Monta PNGs a partir do que o asset tap gravou.
 *
 *   node tools/ffta-sprites/compose.js <dir-do-tap> [--limite=N]
 *
 * ENTRADA   frames.jsonl + tiles/ + palettes/ + oam/   (ver asset-tap.h)
 * SAIDA     <dir>/png/frame-NNNNNN.png   a camada de objetos, como na tela
 *           <dir>/png/index.json         quadro -> arquivo, com a origem
 *
 *
 * POR QUE COMPOR, E NAO SO DESPEJAR OS TILES
 *
 * Uma folha crua de tiles de OBJ e quase inutil para julgar o que foi
 * capturado: um feitico do FFTA e montado com dezenas de objetos, cada um
 * apontando para um pedaco da folha, com a sua propria subpaleta e o seu
 * proprio espelhamento. Olhando so a folha nao da para saber o que e cenario,
 * o que e personagem e o que e a magia.
 *
 * Compondo pela OAM, cada quadro sai como aparece na tela -- e ai a sequencia
 * de arquivos E a animacao.
 *
 *
 * O QUE ESTE SCRIPT NAO FAZ
 *
 * Objetos AFINS (rotacionados/escalados) sao desenhados sem a transformacao.
 * A matriz esta na OAM e daria para aplicar, mas para extrair ARTE o que
 * interessa e o desenho base: a rotacao e efeito de apresentacao, e aplica-la
 * so degradaria os pixels com reamostragem. O campo `affine` no index.json
 * marca quais quadros tem objetos assim.
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

// ---------------------------------------------------------------- PNG

function crc32(buf) {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body), 0);
  return Buffer.concat([len, body, crc]);
}

/** RGBA sem filtro -- basta, e evita trazer dependencia so para isto. */
function writePNG(file, width, height, pixels) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 6;   // RGBA
  const stride = width * 4;
  const raw = Buffer.alloc(height * (stride + 1));
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0;
    pixels.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  fs.writeFileSync(file, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]));
}

// ------------------------------------------------------------- GBA OBJ

const TELA_W = 240;
const TELA_H = 160;

/*
 * Tamanho do objeto em pixels, por shape e size.
 * Tabela do hardware (GBATEK, "OBJ Attribute 1").
 */
const TAMANHOS = [
  [[8, 8], [16, 16], [32, 32], [64, 64]],   // shape 0: quadrado
  [[16, 8], [32, 8], [32, 16], [64, 32]],   // shape 1: deitado
  [[8, 16], [8, 32], [16, 32], [32, 64]],   // shape 2: em pe
];

/** BGR555 -> RGBA. O bit 15 nao e alfa no GBA; a transparencia vem do indice 0. */
function cor(bgr555) {
  const r = (bgr555 & 0x1f);
  const g = (bgr555 >> 5) & 0x1f;
  const b = (bgr555 >> 10) & 0x1f;
  // <<3 e nao regra de tres: e o que o hardware faz, e a diferenca aparece
  // (21 vira 168, nao 173).
  return [r << 3, g << 3, b << 3];
}

function compor(obj, tiles, palette, canvas) {
  const [w, h] = TAMANHOS[obj.shape][obj.size];

  /*
   * X tem 9 bits e Y tem 8, e os dois sao COM SINAL na pratica: um objeto
   * parcialmente fora da tela pela esquerda tem x perto de 511. Sem esta
   * conversao ele reaparece do outro lado.
   */
  let x = obj.x >= 256 ? obj.x - 512 : obj.x;
  let y = obj.y >= 160 ? obj.y - 256 : obj.y;

  const tilesPorLinha = obj.c256 ? w / 16 : w / 8;
  const largTiles = w / 8;
  const altTiles = h / 8;

  for (let ty = 0; ty < altTiles; ty++) {
    for (let tx = 0; tx < largTiles; tx++) {
      /*
       * Mapeamento 1D x 2D (bit 6 do DISPCNT).
       *
       * Em 1D os tiles do objeto sao consecutivos. Em 2D cada linha do objeto
       * avanca 32 tiles, porque a area de OBJ e tratada como uma folha de
       * 32 tiles de largura. Trocar um pelo outro nao da erro -- so embaralha
       * a arte, e o sintoma parece corrupcao de dados.
       */
      const passoLinha = tiles.mapping1D ? tilesPorLinha : 32;
      let indice = obj.tile + ty * passoLinha + tx * (obj.c256 ? 2 : 1);

      const base = (indice * 32) & 0x7fff;

      for (let py = 0; py < 8; py++) {
        for (let px = 0; px < 8; px++) {
          let indiceCor;
          if (obj.c256) {
            indiceCor = tiles.data[base + py * 8 + px];
          } else {
            const b = tiles.data[base + py * 4 + (px >> 1)];
            indiceCor = (px & 1) ? (b >> 4) : (b & 0x0f);
          }
          if (indiceCor === 0) {
            continue; // indice 0 e transparente, sempre
          }

          const entrada = obj.c256 ? indiceCor : obj.pal * 16 + indiceCor;
          const [r, g, b2] = cor(palette[entrada]);

          // Espelhamento e do OBJETO inteiro, nao do tile.
          const ox = obj.hflip ? (w - 1 - (tx * 8 + px)) : (tx * 8 + px);
          const oy = obj.vflip ? (h - 1 - (ty * 8 + py)) : (ty * 8 + py);
          const sx = x + ox;
          const sy = y + oy;
          if (sx < 0 || sy < 0 || sx >= TELA_W || sy >= TELA_H) {
            continue;
          }
          const o = (sy * TELA_W + sx) * 4;
          canvas[o] = r;
          canvas[o + 1] = g;
          canvas[o + 2] = b2;
          canvas[o + 3] = 255;
        }
      }
    }
  }
}

// ------------------------------------------------------------------ main

function main() {
  const dir = process.argv[2];
  if (!dir) {
    console.error('uso: node compose.js <dir-do-tap> [--limite=N]');
    process.exit(2);
  }
  const limArg = process.argv.find((a) => a.startsWith('--limite='));
  const limite = limArg ? Number(limArg.slice('--limite='.length)) : Infinity;

  const linhas = fs.readFileSync(path.join(dir, 'frames.jsonl'), 'utf8')
    .trim().split('\n').filter(Boolean).map(JSON.parse)
    .filter((f) => !f.heartbeat && f.objs && f.objs.length);

  const outDir = path.join(dir, 'png');
  fs.mkdirSync(outDir, { recursive: true });

  const cacheTiles = new Map();
  const cachePal = new Map();
  const ler = (cache, sub, hash) => {
    if (!cache.has(hash)) {
      cache.set(hash, fs.readFileSync(path.join(dir, sub, hash + '.bin')));
    }
    return cache.get(hash);
  };

  const index = [];
  let n = 0;
  for (const f of linhas) {
    if (n >= limite) break;

    const tilesBuf = ler(cacheTiles, 'tiles', f.tiles);
    const palBuf = ler(cachePal, 'palettes', f.palette);
    const palette = new Uint16Array(palBuf.buffer, palBuf.byteOffset, palBuf.length / 2);

    const canvas = Buffer.alloc(TELA_W * TELA_H * 4);
    const tiles = { data: tilesBuf, mapping1D: f.objMapping1D };

    /*
     * Prioridade primeiro, indice DEPOIS -- e ao contrario do que parece.
     *
     * No GBA, entre objetos de mesma prioridade ganha o de MENOR indice de
     * OAM. Desenhando do fim para o comeco, o indice menor sobrescreve, que e
     * o resultado certo.
     */
    const ordem = [...f.objs].sort((a, b) => b.i - a.i);
    for (const obj of ordem) {
      compor(obj, tiles, palette, canvas);
    }

    const nome = `frame-${String(f.frame).padStart(6, '0')}.png`;
    writePNG(path.join(outDir, nome), TELA_W, TELA_H, canvas);
    index.push({
      frame: f.frame,
      file: nome,
      objs: f.objs.length,
      affine: f.objs.some((o) => o.affine),
      tiles: f.tiles,
      palette: f.palette,
    });
    ++n;
  }

  fs.writeFileSync(path.join(outDir, 'index.json'), JSON.stringify(index, null, 1));
  console.log(`${n} quadros compostos em ${outDir}`);
  console.log(`estados de tiles distintos: ${cacheTiles.size}   de paleta: ${cachePal.size}`);
  const comAfim = index.filter((i) => i.affine).length;
  if (comAfim) {
    console.log(`${comAfim} quadros tem objeto afim (desenhado sem a transformacao)`);
  }
}

if (require.main === module) main();
