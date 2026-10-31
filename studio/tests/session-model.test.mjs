import test from 'node:test';
import assert from 'node:assert/strict';
import { appendAgentEvent, conversationReplies, filterSessions, groupSessions, markAbandonedSessions, sessionTitle } from '../src/session-model.mjs';

test('session title normalizes whitespace and truncates without losing readability', () => {
  assert.equal(sessionTitle('  Fix   the\nlogin flow  '), 'Fix the login flow');
  assert.equal(sessionTitle('A'.repeat(100), 12), 'AAAAAAAAAAA…');
  assert.equal(sessionTitle(''), 'Nuova sessione');
});

test('session search matches title, task and workspace without changing source order', () => {
  const sessions = [
    { id: '1', title: 'Auth', task: 'Fix sign in', workspace: 'C:/app' },
    { id: '2', title: 'Docs', task: 'Write guide', workspace: 'D:/docs' },
  ];
  assert.deepEqual(filterSessions(sessions, 'SIGN IN').map(s => s.id), ['1']);
  assert.deepEqual(filterSessions(sessions, 'd:/docs').map(s => s.id), ['2']);
  assert.deepEqual(filterSessions(sessions, ''), sessions);
});

test('sessions group into today, yesterday, last seven days and older', () => {
  const now = new Date(2026, 9, 29, 12).getTime();
  const sessions = [
    { id: 'today', createdAt: new Date(2026, 9, 29, 8).getTime() },
    { id: 'yesterday', createdAt: new Date(2026, 9, 28, 8).getTime() },
    { id: 'week', createdAt: new Date(2026, 9, 24, 8).getTime() },
    { id: 'older', createdAt: new Date(2026, 9, 20, 8).getTime() },
  ];
  assert.deepEqual(groupSessions(sessions, now).map(group => [group.label, group.sessions.map(s => s.id)]), [
    ['Oggi', ['today']],
    ['Ieri', ['yesterday']],
    ['Ultimi sette giorni', ['week']],
    ['Meno recenti', ['older']],
  ]);
});

test('a persisted running session is restored as interrupted, never falsely kept live', () => {
  const sessions = [
    { id: 'live', status: 'running', events: [], cancelRequested: true },
    { id: 'done', status: 'completed', events: [] },
  ];
  const restored = markAbandonedSessions(sessions);
  assert.equal(restored[0].status, 'interrupted');
  assert.equal(restored[0].cancelRequested, false);
  assert.equal(restored[0].events.at(-1).type, 'interrupted');
  assert.equal(restored[1].status, 'completed');
  assert.equal(sessions[0].status, 'running');
});

test('conversation view returns only assistant replies, not runtime/tool events', () => {
  const events = [
    { type: 'start', message: 'agent started' },
    { type: 'generation_started', message: 'requesting model turn' },
    { type: 'assistant_final', text: 'Sì, ci sono.' },
    { type: 'verification_started', message: 'running explicit verifier' },
  ];
  assert.deepEqual(conversationReplies(events), [events[2]]);
});

test('an expired approval is closed and cannot look pending in history', () => {
  const approval = { type: 'approval_required', tool_call_id: 'call-1' };
  const events = appendAgentEvent([approval], { type: 'approval_timeout', tool_call_id: 'call-1' });
  assert.equal(events[0]._approvalStatus, 'expired');
  assert.equal(events[1].type, 'approval_timeout');
  assert.equal(approval._approvalStatus, undefined);
});
