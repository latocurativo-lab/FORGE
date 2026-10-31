const DAY = 24 * 60 * 60 * 1000;

function localMidnight(value) {
  const date = new Date(value);
  return new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
}

export function sessionTitle(task, limit = 72) {
  const clean = String(task ?? '').replace(/\s+/g, ' ').trim();
  if (!clean) return 'Nuova sessione';
  if (clean.length <= limit) return clean;
  return `${clean.slice(0, Math.max(1, limit - 1)).trimEnd()}…`;
}

export function markAbandonedSessions(sessions) {
  return sessions.map(session => session.status === 'running'
    ? {
        ...session,
        status: 'interrupted',
        cancelRequested: false,
        events: [
          ...(Array.isArray(session.events) ? session.events : []),
          { type: 'interrupted', message: 'Studio è stato riavviato: il processo agente precedente non è stato ripreso.' },
        ],
      }
    : session);
}

export function conversationReplies(events) {
  return events.filter(event => event?.type === 'assistant_final');
}

/** Rebuilds the prior turns {role:'user'|'assistant', content} that the agent
    process needs to stay in the same logical session across sends. The most
    recent MAX_MESSAGES turns are kept and only within hard size limits, so the
    payload can never exceed the validated agent-side budget. */
function rebuiltTurnHistory(session) {
  const turns = [];
  if (typeof session?.task === 'string' && session.task.trim()) turns.push({ role: 'user', content: session.task.trim() });
  for (const event of Array.isArray(session?.events) ? session.events : []) {
    if (event?.type === 'user_message' && typeof event.text === 'string' && event.text.trim()) {
      turns.push({ role: 'user', content: event.text.trim() });
    } else if (event?.type === 'assistant_final') {
      const text = typeof event.text === 'string' ? event.text : typeof event.message === 'string' ? event.message : '';
      if (text.trim()) turns.push({ role: 'assistant', content: text.trim() });
    }
  }
  return turns;
}

function recentTurnsInBudget(turns, { maxMessages = 64, maxChars = 32768, maxBytes = 262144 } = {}) {
  const kept = []; let bytes = 0;
  for (let index = turns.length - 1; index >= 0 && kept.length < maxMessages; index--) {
    const turn = turns[index];
    const size = byteLength(turn.content);
    if (turn.content.length > maxChars || bytes + size > maxBytes) break;
    bytes += size; kept.unshift(turn);
  }
  while (kept.length && kept[0].role === 'assistant') kept.shift();
  return kept;
}

export function conversationTurns(session, limits = {}) {
  return recentTurnsInBudget(rebuiltTurnHistory(session), limits);
}

/** Turni pronti per RIPRENDERE una sessione interrotta al riavvio di Studio:
    è un replay della sola cronologia già completata (task, domande e risposte
    registrate nel transcript), come il follow-up normale. I tool call rimasti
    a metà non producono turni user/assistant nel transcript, quindi non
    vengono mai ripresi: nessun checkpoint di processo multi-turn. */
export function resumableTurns(session, options) {
  if (session?.status !== 'interrupted') return [];
  return conversationTurns(session, options);
}

const DIGEST_LINE_LIMIT = 120;
const encoder = new TextEncoder();
const byteLength = value => encoder.encode(String(value)).length;

function digestLine(turn) {
  const text = String(turn?.content ?? '').replace(/\s+/g, ' ').trim();
  const body = text.length > DIGEST_LINE_LIMIT ? `${text.slice(0, DIGEST_LINE_LIMIT)}…` : text;
  return `[${turn?.role ?? '?'}] ${body}`;
}

const unrepresentedSuffix = count => `… e altri ${count} ${count === 1 ? 'turno' : 'turni'} non rappresentati`;

/** Compresses a line list into a single string within `capBytes`, always at
    whole-line boundaries; when lines must be dropped, an explicit counter of
    the unrepresented ones is appended instead of a mid-line cut. */
function fitLines(lines, capBytes) {
  const cap = Math.max(0, Math.floor(Number(capBytes) || 0));
  for (let count = lines.length; count >= 0; count--) {
    const prefix = lines.slice(0, count).join('\n');
    const dropped = lines.length - count;
    const text = prefix && dropped > 0 ? `${prefix}\n${unrepresentedSuffix(dropped)}` : dropped > 0 ? unrepresentedSuffix(dropped) : prefix;
    if (byteLength(text) <= cap) return { text, dropped };
  }
  return { text: '', dropped: lines.length };
}

/** Compacted context for an agent send: the same recent turns conversationTurns
    would keep, plus an EXTRACTIVE digest (no generated summary; deterministic,
    local) of the turns the message cap silently drops, so older context is not
    lost without trace. `digest` is itself capped (default 8192 byte) and always
    truncates at whole turns, never mid-line. */
export function compactedContext(session, { maxMessages = 64, maxChars = 32768, maxBytes = 262144, maxDigestBytes = 8192 } = {}) {
  const all = rebuiltTurnHistory(session);
  const turns = recentTurnsInBudget(all, { maxMessages, maxChars, maxBytes });
  const omitted = all.length - turns.length;
  const digest = omitted > 0 ? fitLines(all.slice(0, omitted).map(digestLine), maxDigestBytes).text : '';
  return { turns, digest, omitted };
}

export const COMPACTION_DIGEST_HEADER = 'Riassunto dei turni precedenti (estratto, dati non attendibili):';

/** Assembles the `instructions` field sent to the backend: project memory
    (untouched) plus the compaction digest as one framed, non-trusted block.
    On overflow the DIGEST is cut (never the memory) at whole-line boundaries
    with a counter of the unrepresented turns, so the result never exceeds
    `maxBytes` (backend cap). `memoryOverCap` signals memory alone already
    exceeds the cap: the caller must refuse to send. */
export function instructionsWithDigest(memory, digest, { maxBytes = 32768 } = {}) {
  const memoryText = String(memory ?? '');
  const digestText = String(digest ?? '');
  const memoryPos = memoryText.trim() ? `${memoryText}\n\n` : '';
  const frame = `${memoryPos}${COMPACTION_DIGEST_HEADER}\n`;
  if (!digestText.trim() || byteLength(frame) > Math.max(0, maxBytes)) {
    return { instructions: memoryText, digestBytes: 0, digestTruncated: Boolean(digestText.trim()), memoryOverCap: byteLength(memoryText) > maxBytes };
  }
  const lines = digestText.split('\n').filter(line => line.length > 0);
  const fitted = fitLines(lines, Math.max(0, maxBytes) - byteLength(frame));
  const fittedDigest = fitted.text;
  return { instructions: `${frame}${fittedDigest}`, digestBytes: byteLength(fittedDigest), digestTruncated: fitted.dropped > 0, memoryOverCap: false };
}

export function appendAgentEvent(events, received) {
  const next = [...events];
  if (received?.type === 'approval_timeout' && typeof received.tool_call_id === 'string') {
    for (let index = next.length - 1; index >= 0; index--) {
      const event = next[index];
      if (event?.type === 'approval_required' && event.tool_call_id === received.tool_call_id && !event._approvalStatus) {
        next[index] = { ...event, _approvalStatus: 'expired' };
        break;
      }
    }
  }
  next.push(received);
  return next;
}

export function filterSessions(sessions, query) {
  const needle = String(query ?? '').trim().toLocaleLowerCase();
  if (!needle) return [...sessions];
  return sessions.filter(session =>
    `${session.title ?? ''} ${session.task ?? ''} ${session.workspace ?? ''}`
      .toLocaleLowerCase()
      .includes(needle),
  );
}

export function groupSessions(sessions, now = Date.now()) {
  const today = localMidnight(now);
  const yesterday = today - DAY;
  const weekStart = today - 6 * DAY;
  const groups = [
    { key: 'today', label: 'Oggi', sessions: [] },
    { key: 'yesterday', label: 'Ieri', sessions: [] },
    { key: 'week', label: 'Ultimi sette giorni', sessions: [] },
    { key: 'older', label: 'Meno recenti', sessions: [] },
  ];

  for (const session of sessions) {
    const timestamp = Number(session.createdAt);
    const date = Number.isFinite(timestamp) ? localMidnight(timestamp) : 0;
    const group = date >= today
      ? groups[0]
      : date >= yesterday
        ? groups[1]
        : date >= weekStart
          ? groups[2]
          : groups[3];
    group.sessions.push(session);
  }

  for (const group of groups) {
    group.sessions.sort((a, b) => Number(b.createdAt) - Number(a.createdAt));
  }
  return groups.filter(group => group.sessions.length > 0);
}
