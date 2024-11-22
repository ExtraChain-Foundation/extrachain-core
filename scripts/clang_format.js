#!/usr/bin/env node

import { promises as fs } from 'fs';
import path from 'path';
import { execFile } from 'child_process';
import { promisify } from 'util';
import { fileURLToPath } from 'url';

const execFileAsync = promisify(execFile);

const FILE_EXTENSIONS = ['.c', '.cpp', '.h', '.hpp'];
const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(SCRIPT_DIR, '..');

async function formatFile(filePath, styleFile) {
    try {
        await execFileAsync('clang-format', [
            '-style=file:' + styleFile,
            '-i',
            filePath
        ]);
        console.log(`[Format] Formatted: ${filePath}`);
    } catch (error) {
        console.error(`[Format] Error formatting ${filePath}:`, error.message);
    }
}

async function* findFiles(dir) {
    const entries = await fs.readdir(dir, { withFileTypes: true });
    
    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        
        if (entry.isDirectory()) {
            yield* findFiles(fullPath);
        } else if (FILE_EXTENSIONS.some(ext => entry.name.endsWith(ext))) {
            yield fullPath;
        }
    }
}

async function main() {
    const styleFile = path.join(PROJECT_ROOT, '.clang-format');
    
    try {
        await fs.access(styleFile);
    } catch {
        console.error('[Format] Error: .clang-format not found in project root');
        process.exit(1);
    }

    console.log('[Format] Starting formatting...');
    
    for await (const file of findFiles(PROJECT_ROOT)) {
        await formatFile(file, styleFile);
    }
    
    console.log('[Format] Formatting complete');
}

main().catch(error => {
    console.error('[Format] Fatal error:', error);
    process.exit(1);
});