#!/usr/bin/env node
/**
 * render-object.mjs
 * Headless Three.js renderer — generates a PNG screenshot of a voxel object
 * at the 2:1 dimetric isometric angle against a dark background.
 *
 * Usage: node tools/render-object.mjs <html-file> [output.png] [width] [height]
 *   html-file: path to a self-contained HTML file with the Three.js scene
 *   output.png: output screenshot path (default: /tmp/voxel-preview.png)
 *   width: viewport width (default: 1200)
 *   height: viewport height (default: 900)
 */

import puppeteer from 'puppeteer';
import { resolve } from 'path';

const htmlFile = process.argv[2];
const outputFile = process.argv[3] || '/tmp/voxel-preview.png';
const width = parseInt(process.argv[4] || '1200', 10);
const height = parseInt(process.argv[5] || '900', 10);

if (!htmlFile) {
  console.error('Usage: node tools/render-object.mjs <html-file> [output.png] [width] [height]');
  process.exit(1);
}

const absPath = resolve(htmlFile);

const browser = await puppeteer.launch({
  headless: true,
  args: ['--no-sandbox', '--disable-setuid-sandbox', '--use-gl=swiftshader'],
});

const page = await browser.newPage();
await page.setViewport({ width, height, deviceScaleFactor: 2 });

await page.goto(`file://${absPath}`, { waitUntil: 'networkidle0', timeout: 15000 });

// Wait for Three.js to render at least one frame
await page.waitForFunction(() => window.__rendered === true, { timeout: 10000 }).catch(() => {
  // Fallback: just wait a bit if the flag isn't set
});
await new Promise(r => setTimeout(r, 500));

await page.screenshot({ path: outputFile, type: 'png' });
console.log(`Screenshot saved: ${outputFile}`);

await browser.close();
