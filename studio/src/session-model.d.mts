export type SessionRecord = {
  id?: string;
  title?: string;
  task?: string;
  workspace?: string;
  createdAt: number;
};

export function sessionTitle(task: string, limit?: number): string;
export function markAbandonedSessions<T extends SessionRecord & { status?: string; events?: unknown[]; cancelRequested?: boolean }>(
  sessions: readonly T[],
): Array<Omit<T, 'status'> & { status: string; events: unknown[]; cancelRequested?: boolean }>;
export function conversationReplies<T extends { type?: string }>(events: readonly T[]): T[];
export type ConversationTurn = { role: 'user' | 'assistant'; content: string };
export type ConversationTurnLimits = { maxMessages?: number; maxChars?: number; maxBytes?: number };
export function conversationTurns(
  session: (SessionRecord & { events?: Array<{ type?: string; text?: unknown; message?: unknown }> }) | undefined,
  limits?: ConversationTurnLimits,
): ConversationTurn[];
export function resumableTurns(
  session: (SessionRecord & { status?: string }) | undefined,
  limits?: ConversationTurnLimits,
): ConversationTurn[];
export type CompactedContext = { turns: ConversationTurn[]; digest: string; omitted: number };
export type CompactedContextLimits = ConversationTurnLimits & { maxDigestBytes?: number };
export function compactedContext(
  session: (SessionRecord & { events?: Array<{ type?: string; text?: unknown; message?: unknown }> }) | undefined,
  limits?: CompactedContextLimits,
): CompactedContext;
export const COMPACTION_DIGEST_HEADER: string;
export type AssembledInstructions = {
  instructions: string;
  digestBytes: number;
  digestTruncated: boolean;
  memoryOverCap: boolean;
};
export function instructionsWithDigest(
  memory: string | undefined,
  digest: string | undefined,
  limits?: { maxBytes?: number },
): AssembledInstructions;
export function appendAgentEvent<T extends { type?: string; tool_call_id?: unknown; _approvalStatus?: string }>(
  events: readonly T[], received: T,
): T[];
export function filterSessions<T extends SessionRecord>(sessions: readonly T[], query: string): T[];
export function groupSessions<T extends SessionRecord>(
  sessions: readonly T[],
  now?: number,
): Array<{ key: 'today' | 'yesterday' | 'week' | 'older'; label: string; sessions: T[] }>;
