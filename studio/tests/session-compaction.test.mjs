import test from 'node:test';
import assert from 'node:assert/strict';
import {
  COMPACTION_DIGEST_HEADER,
  compactedContext,
  conversationTurns,
  instructionsWithDigest,
  markAbandonedSessions,
  resumableTurns,
} from '../src/session-model.mjs';

const utf8 = value => Buffer.byteLength(String(value), 'utf8');

/** Sessione grande: task iniziale + `pairs` coppie user/assistant completate. */
function longSession(pairs, marks = 'domanda', replies = 'risposta') {
  const events = [];
  for (let index = 0; index < pairs; index++) {
    events.push({ type: 'user_message', text: `${marks}-${index}` });
    events.push({ type: 'assistant_final', message: `${replies}-${index}` });
  }
  return markAbandonedSessions([{ id: 'ses-big', task: 'task-0', status: 'completed', createdAt: 0, events }])[0];
}

const COUNTER_RE = /^… e altri (\d+) turni? non rappresentati$/;

test('una sessione oltre il cap: turns contiene solo i recenti, il digest contiene i vecchi in ordine, omitted è corretto', () => {
  const session = longSession(200);
  const { turns, digest, omitted } = compactedContext(session);

  // I turni recenti restano identici a quelli che conversationTurns manderebbe.
  assert.deepEqual(turns, conversationTurns(session));
  assert.equal(turns.length, 64);
  assert.equal(turns.at(-1).content, 'risposta-199');

  // 401 turni (task + 200 coppie) - 64 tenuti = 337 omessi.
  assert.equal(omitted, 401 - 64);
  assert.ok(omitted > 0);

  // Il digest parte dai turni più vecchi, in ordine cronologico.
  const lines = digest.split('\n');
  assert.equal(digest, digest.trim());
  assert.ok(lines.length >= 100);
  assert.equal(lines[0], '[user] task-0');
  assert.equal(lines[1], '[user] domanda-0');
  assert.equal(lines[2], '[assistant] risposta-0');
  assert.ok(digest.includes('domanda-1'));
  assert.ok(digest.includes('risposta-1'));
  // I turni tenuti non finiscono nel digest.
  assert.ok(!digest.includes('risposta-199'));
  // Copre l'ultimo turno omesso: 2i+3 = 337 -> i = 167.
  assert.equal(lines.at(-1), '[assistant] risposta-167');
  // Tutti gli omessi sono rappresentati: il contatore non serve.
  assert.ok(!digest.includes('non rappresentati'));
  assert.ok(utf8(digest) <= 8192, `digest di ${utf8(digest)} byte entro il cap di 8192`);
});

test('una sessione piccola: digest vuoto e omitted a zero', () => {
  const session = longSession(2);
  const { turns, digest, omitted } = compactedContext(session);
  assert.equal(omitted, 0);
  assert.equal(digest, '');
  assert.deepEqual(turns, conversationTurns(session));
  assert.equal(turns.length, 5);
});

test('i limiti byte usano UTF-8 anche in WebView, senza dipendere da Buffer di Node', () => {
  const session = {
    task: '😀', // 4 byte UTF-8
    events: [{ type: 'assistant_final', message: 'à' }], // 2 byte UTF-8
  };
  const { turns, digest, omitted } = compactedContext(session, { maxMessages: 64, maxBytes: 5 });
  // Caratteri JS ne conterebbero solo 3 e li farebbero entrare; i byte UTF-8
  // sono 6, quindi nessuno dei due turni rientra senza spezzare la coppia.
  assert.deepEqual(turns, []);
  assert.equal(omitted, 2);
  assert.ok(digest.includes('[user] 😀'));
});

test('il resume di una sessione interrotta passa dalla stessa compattazione: compactedContext coincide con resumableTurns', () => {
  const events = [];
  for (let index = 0; index < 100; index++) {
    events.push({ type: 'user_message', text: `messe-${index}` });
    events.push({ type: 'assistant_final', message: `fatti-${index}` });
  }
  const session = markAbandonedSessions([{ id: 'ses-interr-big', task: 'innesco', status: 'running', createdAt: 0, events }])[0];
  assert.equal(session.status, 'interrupted');

  const { turns, digest, omitted } = compactedContext(session);
  assert.deepEqual(turns, resumableTurns(session));
  // task + 200 eventi user/assistant = 201 turni totali.
  assert.equal(omitted, 201 - turns.length);
  assert.ok(omitted > 0);
  assert.ok(digest.startsWith('[user] innesco'));
  assert.ok(utf8(digest) <= 8192);
});

test('il digest tronca a intero turno con contatore finale, mai a metà riga', () => {
  const session = longSession(200, 'x'.repeat(400), 'y'.repeat(400));
  const { digest, omitted } = compactedContext(session);

  assert.ok(omitted > 64, 'servono abbastanza turni omessi per traboccare il cap del digest');
  assert.ok(utf8(digest) <= 8192, `digest di ${utf8(digest)} byte dentro il cap proprio di 8192`);

  const lines = digest.split('\n');
  const counter = lines.at(-1);
  const match = counter.match(COUNTER_RE);
  assert.ok(match, `l'ultima riga è il contatore dei turni non rappresentati: ${JSON.stringify(counter.slice(0, 60))}`);
  // Il contenuto integrale (400 caratteri) non entra mai: le righe restano intere e troncate a ~120 char.
  assert.ok(!digest.includes('x'.repeat(200)));
  assert.ok(!digest.includes('y'.repeat(200)));
  for (const line of lines.slice(0, -1)) {
    assert.match(line, /^\[(?:user|assistant)\] /, `ogni riga rappresenta un intero turno: ${JSON.stringify(line.slice(0, 40))}`);
    assert.ok(line.length <= '[assistant] '.length + 120 + 1, `riga entro la soglia di 120 caratteri: lunghezza ${line.length}`);
  }
  // Righe rappresentate + contatore = tutti i turni omessi.
  assert.equal(lines.length - 1 + Number(match[1]), omitted);
});

test("l'assemblaggio delle istruzioni: memoria + digest sta nel cap di 32768 e tronca il digest, mai la memoria", () => {
  // Digest sintetico oltre il budget residuo, per esercitare davvero il taglio.
  const longDigest = Array.from({ length: 200 }, (_, index) => `[${index % 2 ? 'assistant' : 'user'}] ${'q'.repeat(120)}…`).join('\n');
  assert.ok(utf8(longDigest) > 12700, 'il digest sintetico deve superare il budget residuo');
  const memory = 'm'.repeat(20000);
  const { instructions, digestBytes, digestTruncated, memoryOverCap } = instructionsWithDigest(memory, longDigest);

  assert.equal(memoryOverCap, false);
  assert.ok(digestTruncated, 'il digest entra tagliato quando memoria + digest non stanno nel cap');
  assert.ok(utf8(instructions) <= 32768, `istruzioni di ${utf8(instructions)} byte dentro il cap di 32768`);
  assert.ok(digestBytes > 0 && digestBytes <= 32768 - utf8(memory));
  // La memoria è preservata integrale: è la parte che non si taglia.
  assert.ok(instructions.startsWith(memory));
  assert.ok(instructions.includes(COMPACTION_DIGEST_HEADER));
  const blockLines = instructions.slice(memory.length).split('\n').filter(line => line.length > 0);
  const counter = blockLines.at(-1);
  const match = counter.match(COUNTER_RE);
  assert.ok(match, `il blocco si chiude con il contatore dei turni non rappresentati: ${JSON.stringify(counter.slice(0, 60))}`);
  // Righe rappresentate nel blocco (senza intestazione né riga contatore) + contatore = tutte le righe del digest originale.
  const represented = blockLines.length - 2; // esclude la riga di intestazione e la riga contatore
  const originalLines = longDigest.split('\n');
  assert.ok(represented < originalLines.length, 'solo una parte delle righe entra nel cap');
  assert.equal(represented + Number(match[1]), originalLines.length);
});

test('memoria breve e digest piccolo: nessun taglio, blocco distinto con framing di dati non attendibili', () => {
  const { instructions, digestBytes, digestTruncated, memoryOverCap } =
    instructionsWithDigest('Nota di progetto.', '[user] prima richiesta\n[assistant] risposta unica.');
  assert.equal(memoryOverCap, false);
  assert.equal(digestTruncated, false);
  assert.ok(instructions.includes('Nota di progetto.'));
  assert.equal(instructions, `Nota di progetto.\n\n${COMPACTION_DIGEST_HEADER}\n[user] prima richiesta\n[assistant] risposta unica.`);
  assert.equal(digestBytes, utf8('[user] prima richiesta\n[assistant] risposta unica.'));
});

test('un digest assente lascia la memoria identica: nessun blocco aggiunto', () => {
  const full = instructionsWithDigest('Nota unica.', '');
  assert.equal(full.instructions, 'Nota unica.');
  assert.equal(full.digestBytes, 0);
  assert.equal(full.digestTruncated, false);
  assert.equal(full.memoryOverCap, false);
});

test('memoria da sola oltre il cap: segnala memoryOverCap e non aggiunge il digest', () => {
  const memory = 'z'.repeat(32769);
  const { instructions, digestBytes, digestTruncated, memoryOverCap } = instructionsWithDigest(memory, '[user] vecchio');
  assert.equal(memoryOverCap, true);
  assert.equal(instructions, memory);
  assert.equal(digestBytes, 0);
  assert.ok(digestTruncated, 'un digest disponibile ma non rappresentabile è comunque segnalato come troncato');
});

test('le righe del digest: spazi compatti, tronca a 120 char con ellissi, contenuto estratto verbatim (nessun riassunto da modello)', () => {
  const events = [
    { type: 'user_message', text: 'domanda  con\nnewline e    spazi    multipli' },
    { type: 'assistant_final', message: 'z'.repeat(300) },
    { type: 'user_message', text: 'ultima domanda' },
    { type: 'assistant_final', message: 'ultima risposta' },
  ];
  const session = { id: 's', task: 'avvio', status: 'completed', createdAt: 0, events };
  // maxMessages piccolo per far finire i primi turni nel digest.
  const { turns, digest, omitted } = compactedContext(session, { maxMessages: 2 });
  assert.deepEqual(turns, [
    { role: 'user', content: 'ultima domanda' },
    { role: 'assistant', content: 'ultima risposta' },
  ]);
  assert.equal(omitted, 3);
  const lines = digest.split('\n');
  assert.equal(lines.length, 3, 'tre turni omessi, tutti rappresentati: nessun contatore per un digest così corto');
  assert.equal(lines[0], '[user] avvio');
  assert.equal(lines[1], '[user] domanda con newline e spazi multipli');
  // Testo oltre i 120 char: primo estratto + ellissi, a fine riga.
  assert.equal(lines[2], `[assistant] ${'z'.repeat(120)}…`);
  assert.ok(!digest.includes('ultima'), 'i turni mantenuti non compaiono nel digest');
});
