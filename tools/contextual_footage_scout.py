#!/usr/bin/env python3
"""Discover contextual footage candidates from a beat plan.

This v0 searches YouTube metadata only via yt-dlp. It does not download video,
claim archival authenticity, or grant reuse rights. Every result remains a
research candidate until a person verifies the clip, source and rights.

Input JSON:
{
  "beats": [{
    "id": "beat_001",
    "narration": "...",
    "entities": ["Orchid Righteous 3D"],
    "era": "1996",
    "place": "California",
    "action": "product demonstration",
    "visual_intent": "contemporary footage of the card in use"
  }]
}

Run:
  python tools/contextual_footage_scout.py beat_plan.json --out candidates.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ARCHIVAL_TERMS = (
    "archival footage",
    "archive video",
    "newsreel",
    "interview",
    "documentary footage",
    "review",
    "demonstration",
    "commercial",
    "factory film",
)


def clean(value: Any) -> str:
    """Normalize a user-authored query value without changing its meaning."""
    if value is None:
        return ""
    return re.sub(r"\s+", " ", str(value)).strip(" ,;\t\r\n")


def beat_queries(beat: dict[str, Any]) -> list[str]:
    """Create focused bilingual query variants for one authored beat."""
    entities = beat.get("entities", [])
    if isinstance(entities, str):
        entities = [entities]
    entities = [clean(value) for value in entities if clean(value)]
    era = clean(beat.get("era"))
    place = clean(beat.get("place"))
    action = clean(beat.get("action"))
    intent = clean(beat.get("visual_intent"))
    queries: list[str] = []

    for entity in entities:
        # Keep discovery queries compact. Long natural-language visual briefs
        # tend to overconstrain search, so intent/action gets a separate query.
        core = " ".join(part for part in (entity, era) if part)
        if core:
            queries.append(core)
        if action:
            queries.append(" ".join(part for part in (core, action) if part))
        if place and action:
            queries.append(" ".join(part for part in (entity, place, action) if part))
        for term in ARCHIVAL_TERMS:
            queries.append(" ".join(part for part in (core, term) if part))
        if intent:
            # Retain only a concise intent phrase in addition to the named entity.
            short_intent = " ".join(intent.split()[:5])
            queries.append(" ".join(part for part in (core, short_intent) if part))

    # A beat may not have named entities yet. Use its editorially written visual
    # intent, never the entire narration paragraph as an unfiltered search query.
    if not queries and intent:
        queries.append(" ".join(part for part in (intent, era, place, "archive footage") if part))

    deduplicated: list[str] = []
    seen: set[str] = set()
    for query in queries:
        query = clean(query)
        key = query.casefold()
        if query and key not in seen:
            seen.add(key)
            deduplicated.append(query)
    return deduplicated


def search_youtube(query: str, limit: int) -> list[dict[str, Any]]:
    """Return metadata-only YouTube candidates. Never downloads media."""
    try:
        import yt_dlp
    except ImportError as exc:
        raise RuntimeError("Missing dependency: install yt-dlp in this Python environment") from exc

    options = {
        "quiet": True,
        "no_warnings": True,
        "extract_flat": "in_playlist",
        "skip_download": True,
        "playlistend": limit,
        "ignoreerrors": True,
    }
    with yt_dlp.YoutubeDL(options) as ydl:
        result = ydl.extract_info(f"ytsearch{limit}:{query}", download=False)

    candidates: list[dict[str, Any]] = []
    for entry in (result or {}).get("entries") or []:
        if not entry:
            continue
        video_id = clean(entry.get("id"))
        webpage_url = clean(entry.get("webpage_url") or entry.get("url"))
        if not webpage_url and video_id:
            webpage_url = f"https://www.youtube.com/watch?v={video_id}"
        candidates.append(
            {
                "candidate_id": f"youtube_{video_id}" if video_id else None,
                "provider": "youtube_discovery",
                "source_title": clean(entry.get("title")),
                "channel": clean(entry.get("uploader") or entry.get("channel")),
                "source_url": webpage_url,
                "video_id": video_id or None,
                "duration_seconds": entry.get("duration"),
                "upload_date": entry.get("upload_date"),
                "view_count": entry.get("view_count"),
                "description": clean(entry.get("description")),
                "license_status": "unknown",
                "rights_status": "unknown",
                "authenticity_status": "unverified",
                "source_in": None,
                "source_out": None,
                "candidate_status": "needs_review",
                "review_notes": "Discovery result only. Verify actual footage, original source, context and reuse terms.",
            }
        )
    return candidates


def load_beats(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    beats = data.get("beats") if isinstance(data, dict) else None
    if not isinstance(beats, list) or not beats:
        raise ValueError("Input must be a JSON object with a non-empty 'beats' array")
    for index, beat in enumerate(beats):
        if not isinstance(beat, dict):
            raise ValueError(f"beats[{index}] must be an object")
        if not clean(beat.get("id")):
            raise ValueError(f"beats[{index}] needs a non-empty 'id'")
        if not beat_queries(beat):
            raise ValueError(f"beats[{index}] needs entities or visual_intent to make queries")
    return beats


def run(input_path: Path, output_path: Path, limit: int, max_queries: int) -> dict[str, Any]:
    beats = load_beats(input_path)
    results: list[dict[str, Any]] = []
    for beat in beats:
        beat_id = clean(beat["id"])
        queries = beat_queries(beat)[:max_queries]
        for query in queries:
            try:
                candidates = search_youtube(query, limit)
                error = None
            except Exception as exc:  # keep a usable dossier if a query fails
                candidates = []
                error = f"{type(exc).__name__}: {exc}"
            results.append(
                {
                    "beat_id": beat_id,
                    "query": query,
                    "provider": "youtube_discovery",
                    "error": error,
                    "candidates": candidates,
                }
            )

    payload = {
        "schema_version": "contextual-footage-scout-v0.1",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "input_file": str(input_path),
        "discovery_only": True,
        "downloads_performed": False,
        "rights_clearance_performed": False,
        "beats": [clean(beat["id"]) for beat in beats],
        "query_results": results,
        "next_human_steps": [
            "Open each promising source and identify precise source_in/source_out timecodes.",
            "Check footage date, location, subjects and whether it actually supports the beat.",
            "Trace reposts back to the originating archive, broadcaster or rights holder.",
            "Record license terms and attribution; keep unknown or research-only footage out of the publishable master.",
        ],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("beat_plan", type=Path, help="JSON file containing a non-empty beats array")
    parser.add_argument("--out", type=Path, default=Path("scout_candidates.json"))
    parser.add_argument("--limit", type=int, default=8, help="Maximum YouTube results per query (default: 8)")
    parser.add_argument("--max-queries", type=int, default=8, help="Maximum query variants per beat (default: 8)")
    args = parser.parse_args()
    if args.limit < 1 or args.max_queries < 1:
        parser.error("--limit and --max-queries must be positive")
    try:
        payload = run(args.beat_plan, args.out, args.limit, args.max_queries)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    count = sum(len(row["candidates"]) for row in payload["query_results"])
    errors = sum(bool(row["error"]) for row in payload["query_results"])
    print(f"Wrote {args.out}: {len(payload['beats'])} beats, {len(payload['query_results'])} queries, {count} candidates, {errors} query errors")
    print("Discovery only: no media downloaded and no rights cleared.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
