import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import Editor, { loader } from '@monaco-editor/react';
import * as monaco from 'monaco-editor';
import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';
import JsonWorker from 'monaco-editor/esm/vs/language/json/json.worker?worker';
import CssWorker from 'monaco-editor/esm/vs/language/css/css.worker?worker';
import HtmlWorker from 'monaco-editor/esm/vs/language/html/html.worker?worker';
import TsWorker from 'monaco-editor/esm/vs/language/typescript/ts.worker?worker';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import './style.css';
import { isDirty } from './state.mjs';
import { appendAgentEvent, compactedContext, filterSessions, groupSessions, instructionsWithDigest, markAbandonedSessions, sessionTitle } from './session-model.mjs';

self.MonacoEnvironment = { getWorker(_id, label) {
  if (label === 'json') return new JsonWorker();
  if (['css', 'scss', 'less'].includes(label)) return new CssWorker();
  if (['html', 'handlebars', 'razor'].includes(label)) return new HtmlWorker();
  if (['typescript', 'javascript'].includes(label)) return new TsWorker();
  return new EditorWorker();
} };
loader.config({ monaco });

type Entry = { name: string; path: string; directory: boolean };
type AgentEvent = { type?: string; [key: string]: unknown; _approvalStatus?: 'approved' | 'rejected' | 'expired' };
type Session = { id: string; task: string; title?: string; workspace: string; mode?: 'chat' | 'coding'; createdAt: number; status: 'running' | 'completed' | 'error' | 'cancelled' | 'interrupted'; cancelRequested?: boolean; events: AgentEvent[] };
const SESSION_KEY = 'forge-studio.sessions.v1';
const CONFIG_KEY = 'forge-studio.agent-config.v1';
const TABS_KEY = 'forge-studio.open-tabs.v1';
const PROJECTS_KEY = 'forge-studio.projects.v1';
type AgentConfig = { verifierExecutable: string; verifierArgs: string; allowExeText: string; mcpServersJson: string };
type BackendStatus = { available: boolean; path: string | null; source: string | null; message: string | null };
const defaultConfig: AgentConfig = { verifierExecutable: '', verifierArgs: '', allowExeText: '', mcpServersJson: '[]' };
const readConfig = (): AgentConfig => { try { return { ...defaultConfig, ...JSON.parse(localStorage.getItem(CONFIG_KEY) || '{}') }; } catch { return defaultConfig; } };
const readSessions = (): Session[] => {
  try {
    const value = JSON.parse(localStorage.getItem(SESSION_KEY) || '[]');
    if (!Array.isArray(value)) return [];
    return markAbandonedSessions(value) as Session[];
  } catch { return []; }
};
const readOpenTabs = (): string[] => { try { const value = JSON.parse(localStorage.getItem(TABS_KEY) || '[]'); return Array.isArray(value) ? value.filter((id): id is string => typeof id === 'string') : []; } catch { return []; } };
const readProjects = (): string[] => { try { const value = JSON.parse(localStorage.getItem(PROJECTS_KEY) || '[]'); return Array.isArray(value) ? value.filter((path): path is string => typeof path === 'string') : []; } catch { return []; } };
const eventText = (event: AgentEvent) => typeof event.text === 'string' ? event.text : typeof event.message === 'string' ? event.message : JSON.stringify(event);

function App() {
  const [root, setRoot] = useState('');
  const [projects, setProjects] = useState<string[]>(readProjects);
  const [projectQuery, setProjectQuery] = useState('');
  const [tree, setTree] = useState<Record<string, Entry[]>>({});
  const [expanded, setExpanded] = useState<Record<string, boolean>>({});
  const [file, setFile] = useState('');
  const [content, setContent] = useState('');
  const [original, setOriginal] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [task, setTask] = useState('');
  const [mode, setMode] = useState<'chat' | 'coding'>('chat');
  const [config, setConfig] = useState<AgentConfig>(readConfig);
  const [backendStatus, setBackendStatus] = useState<BackendStatus | null>(null);
  const [execConfirm, setExecConfirm] = useState<Record<string, boolean>>({});
  const [sessions, setSessions] = useState<Session[]>(readSessions);
  const [sessionStoreReady, setSessionStoreReady] = useState(false);
  const [sessionStoreWritable, setSessionStoreWritable] = useState(false);
  const [openTabs, setOpenTabs] = useState<string[]>(readOpenTabs);
  const [sessionQuery, setSessionQuery] = useState('');
  const [centerView, setCenterView] = useState<'conversation' | 'editor'>('conversation');
  const [draftMode, setDraftMode] = useState(false);
  const [memory, setMemory] = useState('');
  const [agentsInfo, setAgentsInfo] = useState<{ found: boolean; bytes: number } | null>(null);
  const [memorySaved, setMemorySaved] = useState(true);
  type SkillInfo = { id: string; scope: string; name: string; description: string };
  const [skills, setSkills] = useState<SkillInfo[]>([]);
  const [skillWarnings, setSkillWarnings] = useState<string[]>([]);
  const [selectedSkills, setSelectedSkills] = useState<Set<string>>(new Set());
  const memoryHydrated = useRef(false);
  const memoryTimer = useRef<number | undefined>(undefined);
  const runOwners = useRef<Record<string, string>>({});
  const pendingEvents = useRef<Record<string, AgentEvent[]>>({});
  const sessionWriteQueue = useRef<Promise<void>>(Promise.resolve());
  const [activeSession, setActiveSession] = useState(() => localStorage.getItem('forge-studio.active-session.v1') || '');
  const dirty = isDirty(file, content, original);
  const active = draftMode ? undefined : sessions.find(s => s.id === activeSession);
  const runningSession = sessions.find(session => session.status === 'running');
  // Trasparenza compattazione: quante domande/risposte piu' vecchie verranno
  // riassunte (estratte) invece di essere mandate come messaggi completi.
  const contextCompaction = active ? compactedContext(active) : { turns: [], digest: '', omitted: 0 };
  const compactionNote = contextCompaction.omitted > 0
    ? `${contextCompaction.omitted} ${contextCompaction.omitted === 1 ? 'turno precedente incluso' : 'turni precedenti inclusi'} come riassunto (estratto)`
    : '';
  const confirmDiscard = () => !dirty || window.confirm('Ci sono modifiche non salvate. Vuoi scartarle?');

  useEffect(() => {
    let cancelled = false;
    void (async () => {
      try {
        const stored = await invoke<Session[] | null>('load_sessions');
        if (cancelled) return;
        if (stored) { setSessions(markAbandonedSessions(stored) as Session[]); setSessionStoreWritable(true); }
        else {
          // One-way migration: retain the original localStorage copy as a recovery backup.
          const legacy = readSessions();
          setSessions(legacy);
          if (legacy.length) await invoke<void>('save_sessions', { sessions: legacy });
          setSessionStoreWritable(true);
        }
      } catch (e) {
        if (!cancelled) setError(`Archivio transcript non disponibile; cronologia esistente preservata: ${String(e)}`);
      } finally {
        if (!cancelled) setSessionStoreReady(true);
      }
    })();
    return () => { cancelled = true; };
  }, []);
  useEffect(() => {
    if (!sessionStoreReady || !sessionStoreWritable) return;
    sessionWriteQueue.current = sessionWriteQueue.current
      .then(() => invoke<void>('save_sessions', { sessions }))
      .catch(error => { setError(`Salvataggio transcript fallito: ${String(error)}`); });
  }, [sessions, sessionStoreReady, sessionStoreWritable]);
  useEffect(() => {
    try { localStorage.setItem(PROJECTS_KEY, JSON.stringify(projects)); } catch { setError('Impossibile salvare l’elenco progetti.'); }
  }, [projects]);
  useEffect(() => {
    const valid = openTabs.filter(id => sessions.some(session => session.id === id));
    if (valid.length !== openTabs.length) setOpenTabs(valid);
    try { localStorage.setItem(TABS_KEY, JSON.stringify(valid)); } catch { setError('Impossibile salvare le schede sessione.'); }
  }, [openTabs, sessions]);
  useEffect(() => { try { localStorage.setItem(CONFIG_KEY, JSON.stringify(config)); } catch { setError('Impossibile salvare la configurazione agente nel browser.'); } }, [config]);
  useEffect(() => {
    if (!sessions.some(s => s.id === activeSession)) setActiveSession(sessions[0]?.id || '');
    else if (activeSession) localStorage.setItem('forge-studio.active-session.v1', activeSession);
  }, [sessions, activeSession]);

  useEffect(() => {
    let unlisten: (() => void) | undefined;
    void listen<{ sessionId: string; event: AgentEvent }>('agent-event', ({ payload }) => {
      const received: AgentEvent = { ...payload.event, _receivedAt: Date.now() };
      const sessionId = runOwners.current[payload.sessionId] || payload.sessionId;
      setSessions(current => {
        if (!current.some(s => s.id === sessionId)) {
          const queued = pendingEvents.current[sessionId] || [];
           if (!queued.includes(received)) pendingEvents.current[sessionId] = [...queued, received];
          return current;
        }
        return current.map(s => {
          if (s.id !== sessionId) return s;
          const verificationBlocked = s.events.some(event => event.type === 'verification_required' || event.type === 'verification_failed' || (event.type === 'exit' && event.status === 'BLOCKED'));
          const hasProcessExitCode = received.type === 'exit' && typeof received.code === 'number';
          const status = hasProcessExitCode ? (s.cancelRequested ? 'cancelled' : verificationBlocked ? 'error' : received.code === 0 ? 'completed' : 'error') : received.type === 'error' ? 'error' : s.status;
           return { ...s, status, events: appendAgentEvent(s.events, received) };
        });
      });
    }).then(fn => { unlisten = fn; }).catch(e => setError(`Eventi agentici non disponibili: ${String(e)}`));
    return () => unlisten?.();
  }, []);

  useEffect(() => {
    void invoke<BackendStatus>('forge_backend_status').then(setBackendStatus)
      .catch(error => setBackendStatus({ available: false, path: null, source: null, message: String(error) }));
  }, []);

  useEffect(() => {
    if (!root) { setMemory(''); setAgentsInfo(null); memoryHydrated.current = false; setMemorySaved(true); return; }
    let cancelled = false;
    memoryHydrated.current = false;
    void (async () => {
      try { const text = await invoke<string>('load_project_memory', { workspace: root }); if (!cancelled) { setMemory(text || ''); setMemorySaved(true); } }
      catch { if (!cancelled) { setMemory(''); setMemorySaved(true); } }
      finally { if (!cancelled) memoryHydrated.current = true; }
    })();
    void (async () => {
      try { const text = await invoke<string>('read_file', { path: 'AGENTS.md' }); if (!cancelled) setAgentsInfo({ found: true, bytes: text.length }); }
      catch { if (!cancelled) setAgentsInfo({ found: false, bytes: 0 }); }
    })();
    void (async () => {
      try {
        const discovery = await invoke<{ skills: SkillInfo[]; warnings: string[] }>('discover_skills');
        if (!cancelled) { setSkills(discovery.skills || []); setSkillWarnings(discovery.warnings || []); }
      } catch { if (!cancelled) { setSkills([]); setSkillWarnings([]); } }
    })();
    return () => { cancelled = true; };
  }, [root]);
  useEffect(() => {
    if (!memoryHydrated.current || !root) return;
    setMemorySaved(false);
    window.clearTimeout(memoryTimer.current);
    memoryTimer.current = window.setTimeout(() => {
      void (async () => {
        try { await invoke<void>('save_project_memory', { workspace: root, text: memory }); setMemorySaved(true); }
        catch (e) { setError(`Salvataggio memoria fallito: ${String(e)}`); setMemorySaved(false); }
      })();
    }, 900);
  }, [memory, root]);
  useEffect(() => {
    setSelectedSkills(current => {
      const valid = new Set([...current].filter(id => skills.some(s => s.id === id)));
      return valid.size === current.size ? current : valid;
    });
  }, [skills]);
  useEffect(() => () => window.clearTimeout(memoryTimer.current), []);
  useEffect(() => {
    const previousRoot = localStorage.getItem('forge-studio.workspace.v1');
    if (!previousRoot) return;
    void (async () => {
      try { const restored = await invoke<string>('open_workspace', { path: previousRoot }); setRoot(restored); setProjects(current => [restored, ...current.filter(path => path.toLowerCase() !== restored.toLowerCase())]); setTree({ '': await invoke<Entry[]>('list_directory', { path: '' }) }); }
      catch { localStorage.removeItem('forge-studio.workspace.v1'); }
    })();
  }, []);

  async function chooseWorkspace() {
    if (busy || !confirmDiscard()) return;
    try {
      const selected = await open({ directory: true, multiple: false });
      if (!selected) return;
      setBusy(true); setError('');
      const newRoot = await invoke<string>('open_workspace', { path: selected });
      setRoot(newRoot); localStorage.setItem('forge-studio.workspace.v1', newRoot);
      setProjects(current => [newRoot, ...current.filter(path => path.toLowerCase() !== newRoot.toLowerCase())]);
      setFile(''); setContent(''); setOriginal(''); setTree({}); setExpanded({});
      setTree({ '': await invoke<Entry[]>('list_directory', { path: '' }) });
    } catch (e) { setError(String(e)); } finally { setBusy(false); }
  }
  async function chooseForgeBackend() {
    try {
      const selected = await open({ directory: false, multiple: false, filters: [{ name: 'Backend FORGE', extensions: ['exe'] }] });
      if (typeof selected !== 'string') return;
      const status = await invoke<BackendStatus>('set_forge_exe', { path: selected });
      setBackendStatus(status);
      setError('');
    } catch (e) { setError(`Configurazione backend non riuscita: ${String(e)}`); }
  }
  async function selectProject(path: string) {
    if (path === root || busy || active?.status === 'running' || !confirmDiscard()) return;
    try {
      setBusy(true); setError('');
      const selected = await invoke<string>('open_workspace', { path });
      const entries = await invoke<Entry[]>('list_directory', { path: '' });
      setRoot(selected); localStorage.setItem('forge-studio.workspace.v1', selected);
      setProjects(current => [selected, ...current.filter(item => item.toLowerCase() !== selected.toLowerCase())]);
      setFile(''); setContent(''); setOriginal(''); setTree({ '': entries }); setExpanded({});
    } catch (e) { setError(String(e)); }
    finally { setBusy(false); }
  }
  function forgetProject(path: string) {
    if (active?.status === 'running') { setError('Attendi la fine della sessione prima di rimuovere un progetto dall’elenco.'); return; }
    if (path.toLowerCase() === root.toLowerCase()) {
      if (!confirmDiscard()) return;
      setRoot(''); setFile(''); setContent(''); setOriginal(''); setTree({}); setExpanded({});
      localStorage.removeItem('forge-studio.workspace.v1');
    }
    setProjects(current => current.filter(item => item.toLowerCase() !== path.toLowerCase()));
  }
  async function selectEntry(entry: Entry) {
    if (busy) return;
    if (entry.directory) {
      if (expanded[entry.path]) { setExpanded(v => ({ ...v, [entry.path]: false })); return; }
      try { setBusy(true); setError(''); const entries = await invoke<Entry[]>('list_directory', { path: entry.path }); setTree(v => ({ ...v, [entry.path]: entries })); setExpanded(v => ({ ...v, [entry.path]: true })); } catch (e) { setError(String(e)); } finally { setBusy(false); }
    } else {
      if (entry.path === file || !confirmDiscard()) return;
      try { setBusy(true); setError(''); const text = await invoke<string>('read_file', { path: entry.path }); setFile(entry.path); setContent(text); setOriginal(text); } catch (e) { setError(String(e)); } finally { setBusy(false); }
    }
  }
  async function save() {
    if (!dirty || busy) return;
    const snapshot = content;
    try { setBusy(true); setError(''); await invoke('save_file', { path: file, content: snapshot, original }); setOriginal(snapshot); } catch (e) { setError(String(e)); } finally { setBusy(false); }
  }
  async function startAgent(resume = false) {
    if (!root) { setError('Apri prima una cartella di lavoro.'); return; }
    if (!task.trim() || runningSession) return;
    const prompt = task.trim();
    const followUp = !draftMode;
    const targetSession = followUp ? sessions.find(s => s.id === activeSession) : undefined;
    // Il resume di una sessione interrotta è esplicito, solo via bottone
    // "Riprendi": un invio qualunque su una sessione interrupted resta bloccato.
    const resumingInterrupted = followUp && resume && targetSession?.status === 'interrupted';
    if (followUp) {
      if (!targetSession) { setError('Nessuna sessione attiva: apri una scheda sessione o creane una nuova dal bottone Nuova.'); return; }
      if (targetSession.workspace.toLowerCase() !== root.toLowerCase()) { setError('La sessione attiva appartiene a un progetto diverso: apri quel progetto o crea una nuova sessione.'); return; }
      if (targetSession.status === 'interrupted' && !resumingInterrupted) { setError('Questa sessione era in corso al riavvio di Studio: la cronologia è conservata, usa il bottone “Riprendi” per proseguire nella stessa sessione.'); return; }
    }
    // One agent experience: it can answer, inspect, and act according to the
    // request. Permissions remain enforced by the backend tool approval gates.
    const useMode = 'coding';
    const allowExe = config.allowExeText.split(/\r?\n/).map(value => value.trim()).filter(Boolean);
    const absolute = (value: string) => /^[A-Za-z]:[\\/]/.test(value) || /^\\\\[^\\]+\\[^\\]+/.test(value);
    if (allowExe.some(value => !absolute(value))) { setError('La allowlist, se configurata, deve contenere solo percorsi assoluti.'); return; }
    if (config.verifierExecutable.trim() && (!absolute(config.verifierExecutable.trim()) || !allowExe.some(value => value.toLowerCase() === config.verifierExecutable.trim().toLowerCase()))) { setError('Il verifier, se configurato, deve essere assoluto e già presente nella allowlist.'); return; }
    let mcpServers: unknown;
    try { mcpServers = JSON.parse(config.mcpServersJson || '[]'); } catch { setError('Configurazione server MCP: JSON non valido.'); return; }
    if (!Array.isArray(mcpServers) || mcpServers.length > 8) { setError('Configurazione server MCP: inserisci un array JSON (massimo 8 server).'); return; }
    for (const server of mcpServers) {
      if (!server || typeof server !== 'object' || typeof server.id !== 'string' || !/^[A-Za-z0-9_-]{1,127}$/.test(server.id) || typeof server.executable !== 'string' || !absolute(server.executable) || !Array.isArray(server.args) || server.args.some((arg: unknown) => typeof arg !== 'string') || !Array.isArray(server.allowlist) || !server.allowlist.every((path: unknown) => typeof path === 'string' && absolute(path)) || !server.allowlist.some((path: string) => path.toLowerCase() === server.executable.toLowerCase())) { setError('Ogni server MCP deve avere id valido, executable assoluto, args string[], e allowlist assoluta che includa l’eseguibile.'); return; }
    }
    const compaction = followUp && targetSession ? compactedContext(targetSession) : { turns: [], digest: '', omitted: 0 };
    const historyJson = JSON.stringify(compaction.turns);
    // Il digest dei turni scartati entra nelle istruzioni come blocco distinto
    // (dati non attendibili); in caso di overflow viene tagliato il digest,
    // mai la memoria: il pagamento totale resta comunque sotto il cap del backend.
    const assembled = instructionsWithDigest(memory, compaction.digest);
    if (assembled.memoryOverCap) { setError(`La memoria del progetto supera il cap di 32768 byte delle istruzioni: riducila prima di inviare.`); return; }
    const instructions = assembled.instructions;
    setError('');
    try {
      const id = await invoke<string>('start_agent', { task: prompt, workspace: root, mode: useMode, allowExe, verifierExecutable: config.verifierExecutable.trim(), verifierArgs: config.verifierArgs, mcpServersJson: JSON.stringify(mcpServers), historyJson, instructions, skillNames: [...selectedSkills] });
      if (followUp && targetSession) {
        runOwners.current[id] = targetSession.id;
        const turnEvent: AgentEvent = { type: 'user_message', text: prompt, _receivedAt: Date.now() };
        setSessions(current => current.map(s => s.id !== targetSession.id ? s : { ...s, mode: useMode, status: 'running', cancelRequested: false, events: appendAgentEvent(s.events, turnEvent) }));
        setOpenTabs(current => [targetSession.id, ...current.filter(tab => tab !== targetSession.id)]);
        setActiveSession(targetSession.id); setTask('');
      } else {
        runOwners.current[id] = id;
        // The bridge can emit events before this invoke resolves; the listener
        // queues them by ID until the new session is inserted here.
        const initialEvents = pendingEvents.current[id] || [];
        const lastExit = [...initialEvents].reverse().find(event => event.type === 'exit');
        const session: Session = { id, task: prompt, title: sessionTitle(prompt), workspace: root, mode: useMode, createdAt: Date.now(), status: lastExit ? (lastExit.code === 0 ? 'completed' : 'error') : 'running', events: initialEvents };
        delete pendingEvents.current[id];
         setSessions(current => [session, ...current]); setOpenTabs(current => [id, ...current.filter(tab => tab !== id)]); setActiveSession(id); setDraftMode(false); setTask('');
      }
    } catch (e) { setError(String(e)); }
  }
  async function cancelAgent(session: Session) {
    try { await invoke('cancel_agent', { sessionId: session.id }); setSessions(current => current.map(s => s.id === session.id ? { ...s, cancelRequested: true } : s)); }
    catch (e) { setError(`Annullamento fallito: ${String(e)}`); }
  }
  async function answerApproval(session: Session, event: AgentEvent, approved: boolean) {
    const requestId = typeof event.request_id === 'string' ? event.request_id : '';
    if (!requestId) { setError('Richiesta di approvazione senza request_id: impossibile rispondere.'); return; }
    try {
      await invoke('approve_agent', { sessionId: session.id, requestId, approved });
      setSessions(current => current.map(s => s.id !== session.id ? s : { ...s, events: s.events.map(item => item === event ? { ...item, _approvalStatus: approved ? 'approved' : 'rejected' } : item) }));
      setExecConfirm(current => ({ ...current, [requestId]: false }));
    } catch (e) { setError(`Invio risposta approvazione fallito: ${String(e)}`); }
  }
  const approvalKey = (event: AgentEvent) => typeof event.request_id === 'string' ? event.request_id : '';
  const eventValue = (event: AgentEvent, ...keys: string[]) => { for (const key of keys) if (event[key] !== undefined && event[key] !== null) return typeof event[key] === 'string' ? event[key] as string : JSON.stringify(event[key]); return '—'; };
  const approvalPath = (event: AgentEvent) => {
    const direct = eventValue(event, 'path', 'executable', 'target');
    if (direct !== '—') return direct;
    for (const key of ['arguments', 'tool_arguments', 'args']) {
      const raw = event[key];
      try { const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw; if (parsed && typeof parsed === 'object') { const path = (parsed as Record<string, unknown>).path || (parsed as Record<string, unknown>).executable; if (typeof path === 'string') return path; } } catch { /* args are often a plain argument tail */ }
    }
    return '—';
  };
  function openSession(id: string) {
    if (runningSession && runningSession.id !== id) { setError('Concludi o interrompi la sessione attiva prima di passare a un’altra.'); return; }
    setDraftMode(false);
    setActiveSession(id);
    setOpenTabs(current => [id, ...current.filter(tab => tab !== id)]);
  }
  function closeSessionTab(id: string) {
    if (runningSession?.id === id) return;
    const next = openTabs.filter(tab => tab !== id);
    setOpenTabs(next);
    if (activeSession === id) { if (next[0]) openSession(next[0]); else setDraftMode(true); }
  }
  function beginSessionDraft() {
    if (runningSession) return;
    setDraftMode(true);
    setTask('');
    window.setTimeout(() => document.getElementById('agent-task')?.focus(), 0);
  }
  function renameSession(session: Session) {
    const value = window.prompt('Titolo sessione', session.title || sessionTitle(session.task));
    if (value === null) return;
    const title = value.trim();
    if (!title) { setError('Il titolo della sessione non può essere vuoto.'); return; }
    setSessions(current => current.map(item => item.id === session.id ? { ...item, title } : item));
  }
  function deleteSession(session: Session) {
    if (session.status === 'running') { setError('Annulla la sessione prima di rimuoverla dalla cronologia.'); return; }
    if (!window.confirm(`Rimuovere “${session.title || sessionTitle(session.task)}” dalla cronologia locale? I file del progetto non verranno eliminati.`)) return;
    setSessions(current => current.filter(item => item.id !== session.id));
    setOpenTabs(current => current.filter(id => id !== session.id));
    if (activeSession === session.id) setActiveSession('');
  }
  useEffect(() => {
    const key = (e: KeyboardEvent) => { if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); void save(); } };
    const before = (e: BeforeUnloadEvent) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } };
    window.addEventListener('keydown', key); window.addEventListener('beforeunload', before);
    return () => { window.removeEventListener('keydown', key); window.removeEventListener('beforeunload', before); };
  });
  function conversationItems(session: Session | undefined) {
    if (!session) return [];
    const items: Array<{ role: 'user' | 'assistant'; text: string; time: number; incomplete?: boolean }> = [];
    if (session.task) items.push({ role: 'user', text: session.task, time: session.createdAt });
    for (const event of session.events) {
      if (event.type === 'user_message' && typeof event.text === 'string' && event.text.trim()) {
        items.push({ role: 'user', text: event.text.trim(), time: typeof event._receivedAt === 'number' ? event._receivedAt : 0 });
      } else if (event.type === 'assistant_final') {
        const text = typeof event.text === 'string' ? event.text : typeof event.message === 'string' ? event.message : '';
        if (text.trim()) items.push({ role: 'assistant', text: text.trim(), time: typeof event._receivedAt === 'number' ? event._receivedAt : 0 });
      } else if (event.type === 'incomplete') {
        const detail = typeof event.message === 'string' ? event.message.trim() : 'La generazione si è interrotta prima di completare la risposta.';
        items.push({ role: 'assistant', text: `Risposta incompleta — ${detail}`, incomplete: true, time: typeof event._receivedAt === 'number' ? event._receivedAt : 0 });
      }
    }
    return items;
  }
  function renderTree(path: string, depth = 0): React.ReactNode {
    return (tree[path] || []).map(entry => <React.Fragment key={entry.path}><button className={'tree-item ' + (file === entry.path ? 'selected' : '')} style={{ paddingLeft: 12 + depth * 16 }} onClick={() => void selectEntry(entry)} title={entry.path}><span className="tree-icon">{entry.directory ? (expanded[entry.path] ? '⌄' : '›') : '·'}</span><span>{entry.name}</span></button>{entry.directory && expanded[entry.path] && renderTree(entry.path, depth + 1)}</React.Fragment>);
  }
  return <div className="app">
      <header className="app-header"><div className="brand"><span className="mark">F</span><span>FORGE <em>STUDIO</em></span></div><div className="header-project" title={root}>{root ? root.split(/[\\/]/).filter(Boolean).at(-1) : 'Nessun progetto'}</div><nav className="session-tabs" aria-label="Sessioni aperte">{openTabs.map(id => sessions.find(session => session.id === id)).filter((session): session is Session => Boolean(session)).map(session => <div className={'session-tab ' + (!draftMode && session.id === activeSession ? 'active' : '')} key={session.id}><button onClick={() => openSession(session.id)} title={session.workspace}>{session.title || sessionTitle(session.task)}</button><button className="tab-close" disabled={runningSession?.id === session.id} aria-label="Chiudi scheda" onClick={() => closeSessionTab(session.id)}>×</button></div>)}</nav><button className="new-session" disabled={Boolean(runningSession)} onClick={beginSessionDraft} title="Nuova sessione">+ <span>Nuova</span></button><button className="mode-chip" disabled title="Il backend non applica ancora una policy Plan/Build">Build <span>· Plan non disponibile</span></button><button className="open" disabled={busy || Boolean(runningSession)} onClick={() => void chooseWorkspace()}>Apri cartella</button></header>
    <div className="workspace">
      <aside className="explorer"><div className="section-label">PROGETTI <span>{projects.length}</span></div><label className="project-search"><span aria-hidden="true">⌕</span><input value={projectQuery} onChange={event => setProjectQuery(event.target.value)} placeholder="Cerca progetti" aria-label="Cerca progetti" /><button onClick={() => void chooseWorkspace()} disabled={busy || active?.status === 'running'} title="Aggiungi progetto">+</button></label><div className="project-list">{projects.filter(path => path.toLocaleLowerCase().includes(projectQuery.trim().toLocaleLowerCase())).map(path => <div className={'project-item ' + (path.toLowerCase() === root.toLowerCase() ? 'selected' : '')} key={path}><button onClick={() => void selectProject(path)} disabled={busy || active?.status === 'running'} title={path}><span className="project-glyph">▰</span><span>{path.split(/[\\/]/).filter(Boolean).at(-1) || path}</span></button><button className="project-forget" onClick={() => forgetProject(path)} aria-label={`Rimuovi ${path} dall’elenco`} title="Rimuovi dall’elenco; i file non vengono cancellati">×</button></div>)}{projects.length === 0 && <p className="aside-empty">Aggiungi una cartella per iniziare.</p>}</div><div className="section-label file-section-label">FILE <span>{root ? 'WORKSPACE' : '—'}</span></div>{root ? <><div className="root" title={root}>{root}</div><div className="tree">{renderTree('')}</div></> : <p className="aside-empty">Seleziona un progetto per esplorarne i file.</p>}</aside>
       <main className="session-main"><div className="conversation-header"><div className="conversation-title"><span className="status-indicator"/><div><strong>{active?.title || (active ? sessionTitle(active.task) : root ? 'Nuova sessione' : 'FORGE Studio')}</strong><small>{root || 'Apri un progetto per iniziare'}{active ? ` · ${active.status === 'interrupted' ? 'interrotta · riprendibile' : active.status}` : ''}</small></div></div><div className="view-switch" role="tablist" aria-label="Vista principale"><button role="tab" aria-selected={centerView === 'conversation'} className={centerView === 'conversation' ? 'selected' : ''} onClick={() => setCenterView('conversation')}>Conversazione</button><button role="tab" aria-selected={centerView === 'editor'} className={centerView === 'editor' ? 'selected' : ''} onClick={() => setCenterView('editor')}>Editor</button></div><span className="runtime-unverified" title="Le metriche runtime non sono ancora esposte dal backend Studio">Runtime non verificato</span></div>{error && <div className="error" role="alert"><span>{error}</span><button onClick={() => setError('')} aria-label="Chiudi errore">×</button></div>}{centerView === 'editor' ? <><div className="editor-header"><span>{file || 'Nessun file aperto'}{dirty && <b className="dirty"> ●</b>}</span><button disabled={!dirty || busy} onClick={() => void save()}>Salva <kbd>Ctrl S</kbd></button></div>{file ? <Editor path={file} value={content} onChange={v => setContent(v ?? '')} theme="vs-dark" options={{ fontSize: 14, minimap: { enabled: false }, automaticLayout: true, scrollBeyondLastLine: false, padding: { top: 18 } }} /> : <div className="welcome"><span className="welcome-icon">/ /</span><h1>Editor</h1><p>Seleziona un file dal progetto per aprirlo.<br/>La conversazione resta disponibile nella scheda dedicata.</p><button onClick={() => void chooseWorkspace()} disabled={busy}>Apri cartella <span>↗</span></button></div>}</> : <><div className="conversation-scroll">{active ? <>{conversationItems(active).map((item, index) => item.role === 'user' ? <article className="user-message" key={`${active.id}-turn-${index}`}><div className="message-author">TU {item.time ? <time>{new Date(item.time).toLocaleString()}</time> : null}</div><p>{item.text}</p></article> : <article className="assistant-message" key={`${active.id}-turn-${index}`}><div className="message-author">FORGE</div><p>{item.text}</p></article>)}{active.status === 'running' && <div className="reply-pending" role="status"><span className="reply-pulse"/>FORGE sta rispondendo…</div>}<div className="timeline" aria-label="Attività osservate">{active.events.map((event, index) => { const kind = String(event.type || 'event'); const label = kind === 'tool_request' ? 'Tool richiesto' : kind === 'tool_result' ? 'Tool completato' : kind === 'tool_denied' ? 'Tool negato' : kind === 'tool_failure' ? 'Tool in errore' : kind === 'assistant_final' ? 'Risposta FORGE' : kind === 'verification_started' ? 'Verifica avviata' : kind === 'verification_passed' ? 'Verifica superata' : kind === 'verification_failed' ? 'Verifica fallita' : kind === 'approval_required' ? 'Autorizzazione richiesta' : kind === 'interrupted' ? 'Sessione interrotta dal riavvio Studio' : kind; return <details className={'timeline-event ' + (kind === 'error' || kind === 'tool_failure' || kind === 'verification_failed' || kind === 'interrupted' ? 'failed' : '')} key={`${active.id}-timeline-${index}`}><summary><span className="timeline-mark"/><strong>{label}</strong><time>{typeof event._receivedAt === 'number' ? new Date(event._receivedAt).toLocaleTimeString() : ''}</time><span className="timeline-chevron">⌄</span></summary><pre>{eventText(event)}</pre></details>; })}</div>{active.status === 'interrupted' && <div className="timeline-empty resume-offer">Questa sessione era in corso al riavvio di Studio. La cronologia è conservata: scrivi un nuovo messaggio e premi “Riprendi” per riprendere dalla stessa conversazione.{task.trim() && root && !runningSession && <button className="run resume-button" onClick={() => void startAgent(true)}>Riprendi <span>↗</span></button>}</div>}{active.events.length === 0 && active.status === 'running' && <p className="timeline-empty">In attesa dei primi eventi del backend…</p>}</> : <div className="conversation-empty"><span className="empty-mark">F</span><h1>Che cosa vuoi costruire?</h1><p>Apri un progetto, scrivi una richiesta e segui nella timeline gli eventi reali dell’agente.</p>{!root && <button onClick={() => void chooseWorkspace()}>Apri progetto</button>}</div>}</div><div className="composer"><textarea id="agent-task" value={task} onChange={e => setTask(e.target.value)} onKeyDown={e => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); void startAgent(); } }} placeholder={root ? (mode === 'chat' ? 'Scrivi a FORGE…' : 'Descrivi il task di coding…') : 'Apri un progetto prima di inviare una richiesta'} rows={3} disabled={active?.status === 'running' || !root} aria-label="Richiesta per FORGE"/>{compactionNote && <div className="compaction-note" role="note" title="I turni più vecchi non superano il cap dei messaggi: entrano come estratto troncato nelle istruzioni. Testo estratto, dati non attendibili.">{compactionNote}</div>}<div className="composer-toolbar"><div className="composer-mode"><button className={mode === 'chat' ? 'active-mode' : ''} onClick={() => setMode('chat')} disabled={Boolean(runningSession)}>Conversazione</button><button className={mode === 'coding' ? 'active-mode' : ''} onClick={() => setMode('coding')} disabled={Boolean(runningSession)}>Coding</button><span className="model-label">Runtime locale · modello non verificato</span></div><div className="composer-actions">{active?.status === 'running' && !active.cancelRequested ? <button className="cancel" onClick={() => void cancelAgent(active)}>Interrompi</button> : active?.cancelRequested ? <span className="cancel-wait">Interruzione in attesa…</span> : null}<button className="run" onClick={() => void startAgent(active?.status === 'interrupted')} disabled={!root || !task.trim() || active?.status === 'running'}>{active?.status === 'running' ? 'In esecuzione' : active?.status === 'interrupted' ? 'Riprendi' : 'Invia'} <span>↗</span></button></div></div><div className="composer-hint">Invio per inviare · Shift+Invio per andare a capo</div></div></>}</main>
       <aside className="agent"><div className="section-label">SESSIONE <span>{active ? active.status.toUpperCase() : 'LOCALE'}</span></div>
          <details className="agent-config"><summary>MEMORIA E ISTRUZIONI DEL PROGETTO</summary><div className="context-note" style={{ display: 'grid', gap: 6 }}><small>AGENTS.md: {agentsInfo?.found ? `caricato (${agentsInfo.bytes} byte)` : agentsInfo ? 'assente nel progetto' : '…'}</small><small>Memoria progetto: {memory.length} byte · {memorySaved ? 'salvata' : 'salvataggio in corso…'}</small><small>Queste fonti vengono inviate a FORGE a ogni invio come contesto non attendibile: non concedono permessi e non sono comandi.</small></div><textarea id="project-memory" className="memory-editor" value={memory} onChange={e => setMemory(e.target.value)} placeholder={'Note persistenti per questo progetto:\ndecisioni, preferenze, contesto da ricordare…'}  rows={6} style={{ width: '100%', marginTop: 8 }} /></details>
          <details className="agent-config"><summary>SKILL DEL PROGETTO ({skills.length})</summary><div className="context-note" style={{ display: 'grid', gap: 6 }}>
            {skills.length === 0 && <small>Nessuna skill trovata. Creane una in <code>.forge/skills/&lt;id&gt;/SKILL.md</code> (o <code>.agents</code>, <code>.claude</code>, <code>.opencode</code>) nel progetto e riapri la cartella.</small>}
            {skills.map(skill => (
              <label key={`${skill.scope}/${skill.id}`} style={{ display: 'grid', gridTemplateColumns: 'auto 1fr', gap: 6, alignItems: 'start' }}>
                <input type="checkbox" checked={selectedSkills.has(skill.id)} onChange={e => setSelectedSkills(current => { const next = new Set(current); if (e.target.checked) next.add(skill.id); else next.delete(skill.id); return next; })} />
                <span><strong>{skill.name}</strong>{skill.scope !== '.forge' && <small> · {skill.scope}</small>}{skill.description && <small style={{ display: 'block' }}>{skill.description}</small>}</span>
              </label>
            ))}
            {selectedSkills.size > 0 && <small>{selectedSkills.size} skill caricate a ogni invio come contesto non attendibile (max 8 KiB l'una). Non concedono permessi.</small>}
            {skillWarnings.map((warning, index) => <small key={`warn-${index}`} style={{ color: '#a26b00' }}>{warning}</small>)}
          </div></details>
          <details className="agent-config"><summary>BACKEND, VERIFIER E MCP</summary><div className={'backend-status ' + (backendStatus?.available ? 'available' : 'missing')}><span className="backend-dot"/><div><strong>{backendStatus?.available ? 'Backend FORGE disponibile' : backendStatus ? 'Backend FORGE non configurato' : 'Ricerca backend FORGE…'}</strong><small>{backendStatus?.path || backendStatus?.message || 'Controllo percorsi di installazione'}</small>{backendStatus?.source && <small>Origine: {backendStatus.source}</small>}</div></div><button className="backend-select" onClick={() => void chooseForgeBackend()} disabled={Boolean(runningSession)}>Seleziona forge.exe…</button><label htmlFor="verify-exe">ESEGUIBILE VERIFIER (PERCORSO ASSOLUTO)</label><input id="verify-exe" value={config.verifierExecutable} onChange={e => setConfig(c => ({ ...c, verifierExecutable: e.target.value }))} placeholder="C:\\tools\\ctest.exe" disabled={active?.status === 'running'} /><label htmlFor="verify-args">ARGOMENTI VERIFIER (CODA ESATTA)</label><input id="verify-args" value={config.verifierArgs} onChange={e => setConfig(c => ({ ...c, verifierArgs: e.target.value }))} placeholder="--test-dir build --output-on-failure" disabled={active?.status === 'running'} /><label htmlFor="allow-exe">ESEGUIBILI AUTORIZZATI (UNO PER RIGA, ASSOLUTI)</label><textarea id="allow-exe" value={config.allowExeText} onChange={e => setConfig(c => ({ ...c, allowExeText: e.target.value }))} placeholder={'C:\\tools\\ctest.exe'} rows={3} disabled={active?.status === 'running'} /><small>Il verifier deve comparire nella allowlist. L'approvazione di una richiesta non la modifica.</small><label htmlFor="mcp-servers">SERVER MCP (JSON, opzionale)</label><textarea id="mcp-servers" value={config.mcpServersJson} onChange={e => setConfig(c => ({ ...c, mcpServersJson: e.target.value }))} placeholder={'[{"id":"local","executable":"C:\\\\tools\\\\server.exe","args":[],"allowlist":["C:\\\\tools\\\\server.exe"]}]'} rows={5} disabled={active?.status === 'running'} /><small>Ogni avvio server richiede approvazione; ogni chiamata MCP è poi approvata separatamente e trattata come potenzialmente mutativa.</small></details>
          <div className="session-heading"><span>SESSIONI</span><small>{sessions.length}</small></div>
          <label className="session-search"><span aria-hidden="true">⌕</span><input value={sessionQuery} onChange={event => setSessionQuery(event.target.value)} placeholder="Cerca sessioni" aria-label="Cerca sessioni" /></label>
          <div className="session-list">{groupSessions(filterSessions(sessions, sessionQuery)).length ? groupSessions(filterSessions(sessions, sessionQuery)).map(group => <section className="session-group" key={group.key}><h3>{group.label}</h3>{group.sessions.map(session => <div key={session.id} className={'session-item-wrap ' + (session.id === activeSession ? 'current' : '')}><button className="session-item" onClick={() => openSession(session.id)} title={session.workspace}><span className={'session-dot ' + session.status}/><span className="session-copy"><strong>{session.title || sessionTitle(session.task)}</strong><small>{new Date(session.createdAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })} · {session.events.length} eventi</small></span></button><button className="session-menu" aria-label={`Rinomina ${session.title || sessionTitle(session.task)}`} title="Rinomina sessione" onClick={() => renameSession(session)}>···</button><button className="session-menu session-remove" aria-label={`Rimuovi ${session.title || sessionTitle(session.task)}`} title="Rimuovi dalla cronologia" onClick={() => deleteSession(session)}>×</button></div>)}</section>) : <p className="session-empty">{sessions.length ? 'Nessuna sessione corrisponde alla ricerca.' : 'Le sessioni completate appariranno qui.'}</p>}</div>
          <div className="event-heading"><span>ATTIVITÀ E APPROVAL</span><span>{active ? active.status.toUpperCase() : 'NESSUNA SESSIONE'}</span></div><div className="event-list">{active?.events.length ? active.events.map((event, index) => { const isApproval = event.type === 'approval_required'; const requestId = approvalKey(event); const tool = eventValue(event, 'tool', 'tool_name', 'name'); const isExec = /exec|run_command|ft_run_approved/i.test(tool) || /^exec$/i.test(eventValue(event, 'capability')); const answered = event._approvalStatus; return <article className={'event ' + (isApproval ? 'approval-event' : '')} key={`${active.id}-${index}`}><div><b>{event.type === 'approval_timeout' ? 'APPROVAL SCADUTA' : isApproval ? 'APPROVAZIONE RICHIESTA' : String(event.type || 'event')}</b><time>{typeof event._receivedAt === 'number' ? new Date(event._receivedAt).toLocaleTimeString() : ''}</time></div>{isApproval ? <>{!answered && <p className="approval-wait">In attesa della tua scelta · scade dopo 30 secondi</p>}<dl className="approval-details"><dt>Tool</dt><dd>{tool}</dd><dt>Path / eseguibile</dt><dd>{approvalPath(event)}</dd><dt>Args</dt><dd>{eventValue(event, 'args', 'arguments', 'tool_arguments')}</dd><dt>Call ID</dt><dd>{eventValue(event, 'call_id', 'tool_call_id', 'callId', 'id')}</dd><dt>Capability</dt><dd>{eventValue(event, 'capability')}</dd><dt>Mutation</dt><dd>{event.mutation === true || event.mutation_required === true || event.mutation_flag === true ? 'SÌ' : event.mutation === false || event.mutation_required === false || event.mutation_flag === false ? 'NO' : 'Non specificato'}</dd><dt>Request ID</dt><dd>{requestId || 'Mancante'}</dd></dl>{isExec && !answered && <label className="exec-confirm"><input type="checkbox" checked={Boolean(execConfirm[requestId])} onChange={e => setExecConfirm(current => ({ ...current, [requestId]: e.target.checked }))} />Confermo separatamente questa esecuzione esatta (eseguibile e argomenti mostrati).</label>}{answered ? <p className="approval-status">{answered === 'approved' ? 'Approvata esplicitamente' : answered === 'expired' ? 'Scaduta · nessuna azione eseguita' : 'Rifiutata esplicitamente'}</p> : <div className="approval-actions"><button className="approve" disabled={!requestId || (isExec && !execConfirm[requestId]) || active.status !== 'running'} onClick={() => void answerApproval(active, event, true)}>Approva</button><button className="reject" disabled={!requestId || active.status !== 'running'} onClick={() => void answerApproval(active, event, false)}>Rifiuta</button></div>}</> : <pre>{eventText(event)}</pre>}</article>; }) : <p className="session-empty">Nessun evento. Gli eventi restituiti dal backend compariranno in questo pannello.</p>}</div>
      </aside>
    </div><footer><span><i/> LOCALE</span><span>{root || 'Nessuna cartella'}</span><span className="footer-right">{dirty ? 'MODIFICHE NON SALVATE' : file ? 'SALVATO' : 'PRONTO'}</span></footer>
  </div>;
}

type AppBoundaryState = { error: Error | null };
class AppBoundary extends React.Component<React.PropsWithChildren, AppBoundaryState> {
  state: AppBoundaryState = { error: null };

  static getDerivedStateFromError(error: Error): AppBoundaryState {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    const report = (window as Window & { __FORGE_UI_FAIL__?: (message: string) => void }).__FORGE_UI_FAIL__;
    report?.(`${error.stack || error.message}\n${info.componentStack || ''}`);
  }

  componentDidMount() {
    if (!this.state.error) {
      (window as Window & { __FORGE_UI_MOUNTED__?: boolean }).__FORGE_UI_MOUNTED__ = true;
      (window as Window & { __FORGE_UI_READY__?: () => void }).__FORGE_UI_READY__?.();
    }
  }

  render() {
    if (this.state.error) {
      return <main role="alert" style={{ minHeight: '100vh', padding: 32, background: '#111411', color: '#dbddd8', fontFamily: 'system-ui, sans-serif' }}>
        <h1>FORGE Studio non è riuscito a disegnare l’interfaccia</h1>
        <p>I dati delle sessioni non sono stati cancellati. Dettaglio tecnico:</p>
        <pre style={{ maxHeight: '55vh', overflow: 'auto', whiteSpace: 'pre-wrap', color: '#f0b5a8' }}>{this.state.error.stack || this.state.error.message}</pre>
        <button onClick={() => window.location.reload()}>Ricarica Studio</button>
      </main>;
    }
    return this.props.children;
  }
}

createRoot(document.getElementById('root')!).render(<AppBoundary><App /></AppBoundary>);
