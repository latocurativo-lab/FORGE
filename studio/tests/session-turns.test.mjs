import test from 'node:test';
import assert from 'node:assert/strict';
import { conversationTurns } from '../src/session-model.mjs';

test('conversation turns rebuild task and assistant replies in order', () => {
  const session = {
    task: 'chi sei?',
    events: [
      { type: 'start' },
      { type: 'assistant_final', message: 'Sono FORGE.' },
      { type: 'user_message', text: 'e come funziona la memoria?' },
      { type: 'assistant_final', message: 'Salva note per progetto e le reinvio.' },
      { type: 'tool_request' },
      { type: 'tool_result' },
      { type: 'exit', code: 0 },
    ],
  };
  const turns = conversationTurns(session);
  assert.deepEqual(turns, [
    { role: 'user', content: 'chi sei?' },
    { role: 'assistant', content: 'Sono FORGE.' },
    { role: 'user', content: 'e come funziona la memoria?' },
    { role: 'assistant', content: 'Salva note per progetto e le reinvio.' },
  ]);
});

test('conversation turns keep only the most recent messages within hard caps', () => {
  const events = [];
  for (let index = 0; index < 300; index++) {
    events.push({ type: 'assistant_final', message: `reply-${index}` });
    events.push({ type: 'user_message', text: `q-${index}` });
  }
  const session = { task: 'task-0', events };
  const turns = conversationTurns(session);
  assert.ok(turns.length <= 64);
  assert.equal(turns.at(-1).role, 'user');
  assert.equal(turns.at(-1).content, 'q-299');
  assert.ok(!turns.some(t => t.content.includes('reply-100') || t.content.includes('q-10-')));
});

test('oversized or unavailable sessions produce a bounded or empty history, never crash', () => {
  const huge = 'x'.repeat(40000);
  const turns = conversationTurns({ task: huge, events: [] });
  assert.deepEqual(turns, []);
  assert.deepEqual(conversationTurns(undefined), []);
});
