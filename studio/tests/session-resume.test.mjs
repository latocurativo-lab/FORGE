import test from 'node:test';
import assert from 'node:assert/strict';
import { conversationTurns, markAbandonedSessions, resumableTurns } from '../src/session-model.mjs';

/** Sessione tipica di una run interrotta al riavvio di Studio: task iniziale,
    due turni completati e poi un tool call rimasto a metà (niente tool_result)
    più l'evento `interrupted` che markAbandonedSessions appone alla riapertura. */
function interruptedSessionWithDanglingToolCall() {
  const saved = {
    id: 'ses-interr',
    task: 'riorganizza i test del modulo anagrafica',
    status: 'running',
    createdAt: Date.now(),
    events: [
      { type: 'start' },
      { type: 'assistant_final', message: 'Prima risposta completata: piano a tre passi.' },
      { type: 'user_message', text: 'ok, appendi anche la copertura dei test' },
      { type: 'assistant_final', message: 'Fatto, aggiungo la copertura.' },
      { type: 'tool_request', tool: 'ft_write_file', tool_call_id: 'call-1' },
      { type: 'approval_required', tool_call_id: 'call-1', request_id: 'req-1' },
    ],
  };
  return markAbandonedSessions([saved])[0];
}

test('una sessione interrotta resta completamente riprendibile: task e risposte completate sono nei turni ricostruiti', () => {
  const session = interruptedSessionWithDanglingToolCall();
  assert.equal(session.status, 'interrupted');

  const turns = resumableTurns(session);
  assert.equal(turns.length, 4);
  assert.deepEqual(turns, [
    { role: 'user', content: 'riorganizza i test del modulo anagrafica' },
    { role: 'assistant', content: 'Prima risposta completata: piano a tre passi.' },
    { role: 'user', content: 'ok, appendi anche la copertura dei test' },
    { role: 'assistant', content: 'Fatto, aggiungo la copertura.' },
  ]);

  // La cronologia riprende sempre da un turno utente (mai una risposta orfana in testa).
  assert.equal(turns[0].role, 'user');
  for (const turn of turns) {
    assert.ok(turn.role === 'user' || turn.role === 'assistant');
    assert.ok(turn.content.trim().length > 0);
  }
});

test('i turni riprendibili non includono mai un tool call non concluso (tool_request senza tool_result correlato)', () => {
  const session = interruptedSessionWithDanglingToolCall();
  const turns = resumableTurns(session);
  const flattened = JSON.stringify(turns);
  // Il tool_request e la sua approval sono rimasti appesi a metà run: nessuno
  // dei due deve finire nel replay della cronologia in nessuna forma.
  assert.ok(!flattened.includes('ft_write_file'));
  assert.ok(!flattened.includes('tool_request'));
  assert.ok(!flattened.includes('tool_result'));
  assert.ok(!flattened.includes('approval_required'));
  assert.ok(!flattened.includes('call-1'));
  assert.ok(!flattened.includes('req-1'));
});

test('il resume è replay della cronologia, non un checkpoint di processo: stesso cap del follow-up normale', () => {
  const events = [];
  for (let index = 0; index < 200; index++) {
    events.push({ type: 'user_message', text: `domanda-${index}` });
    events.push({ type: 'assistant_final', message: `risposta-${index}` });
    // metà delle domande con un tool call rimasto in sospeso dietro ad esse
    if (index % 2 === 0) events.push({ type: 'tool_request', tool_call_id: `pending-${index}` });
  }
  const session = markAbandonedSessions([{ id: 's', task: 'task iniziale', status: 'running', createdAt: 0, events }])[0];

  const turns = resumableTurns(session);
  assert.ok(turns.length <= 64, 'la history inviata rispetta il cap dei 64 messaggi');
  // l'ultimo turno è il completamento più recente della conversazione
  assert.equal(turns.at(-1).role, 'assistant');
  assert.equal(turns.at(-1).content, 'risposta-199');
  // i turni conservati seguono l'alternanza user/assistant già completata
  for (let index = 1; index < turns.length; index++) {
    assert.notEqual(turns[index].role, turns[index - 1].role);
  }
  assert.ok(!JSON.stringify(turns).includes('pending-'), 'nessun tool call pendente nel replay');
});

test('resumableTurns risponde solo per status interrupted: gli altri stati non sono riprendibili via replay', () => {
  const base = { id: 's', task: 't', createdAt: 0, events: [{ type: 'assistant_final', message: 'r' }] };
  assert.deepEqual(resumableTurns({ ...base, status: 'running' }), []);
  assert.deepEqual(resumableTurns({ ...base, status: 'completed' }), []);
  assert.deepEqual(resumableTurns({ ...base, status: 'error' }), []);
  assert.deepEqual(resumableTurns(undefined), []);
  const resumed = resumableTurns({ ...base, status: 'interrupted' });
  assert.deepEqual(resumed, [
    { role: 'user', content: 't' },
    { role: 'assistant', content: 'r' },
  ]);
});

test('una sessione con task troppo grande per i cap produce una history vuota ma resta riprendibile come nuova run', () => {
  const huge = 'x'.repeat(40000);
  const session = markAbandonedSessions([{ id: 's', task: huge, status: 'running', createdAt: 0, events: [] }])[0];
  assert.equal(session.status, 'interrupted');
  assert.deepEqual(resumableTurns(session), []);
});

test('un assistant incompleto (mai assistant_final) prima dell\'interruzione non entra nel replay', () => {
  const session = markAbandonedSessions([{
    id: 's',
    task: 'scrivi la relazione',
    status: 'running',
    createdAt: 0,
    events: [
      { type: 'assistant_delta', text: 'risposta tronca…' },
      { type: 'tool_request', tool: 'ft_read_file', tool_call_id: 'call-x' },
    ],
  }])[0];
  const turns = conversationTurns(session);
  assert.deepEqual(turns, [{ role: 'user', content: 'scrivi la relazione' }]);
});
