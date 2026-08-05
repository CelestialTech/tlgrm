#!/usr/bin/env node
/**
 * viewport.mjs
 * Opens a chrome-less browser window for live 3D object preview.
 * Keeps running — use Ctrl+C to close.
 *
 * Usage: node tools/viewport.mjs [width] [height]
 */

import puppeteer from 'puppeteer';
import { resolve } from 'path';

const width = parseInt(process.argv[2] || '1200', 10);
const height = parseInt(process.argv[3] || '900', 10);

const htmlPath = resolve('tools/preview.html');

const browser = await puppeteer.launch({
  headless: false,
  args: [
    `--app=file://${htmlPath}`,
    `--window-size=${width},${height}`,
    '--disable-extensions',
    '--hide-scrollbars',
  ],
  defaultViewport: null,
});

const pages = await browser.pages();
const page = pages[0] || await browser.newPage();

// Expose a function to take screenshots from the Node side
page.on('console', msg => {
  if (msg.type() === 'log') console.log('[viewport]', msg.text());
});

console.log(`Viewport open (${width}x${height}). PID: ${browser.process().pid}`);
console.log('Browser stays open. Press Ctrl+C to close.');

// Keep process alive
process.on('SIGINT', async () => {
  await browser.close();
  process.exit(0);
});

// Wait forever
await new Promise(() => {});
