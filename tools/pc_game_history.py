"""Persistent, validated history of games observed by Breeze PC."""
import re


_TID = re.compile(r"^[0-9a-fA-F]{16}$")
MAX_HISTORY = 20


def valid_tid(value):
    """Return an uppercase 16-hex title ID, or an empty string."""
    value = str(value or "").strip()
    return value.upper() if _TID.fullmatch(value) and value != "0" * 16 else ""


def remember(history, tid, name="", limit=MAX_HISTORY):
    """Put a valid game first, updating its name and removing duplicate IDs."""
    tid = valid_tid(tid)
    if not tid:
        return clean(history, limit)
    name = str(name or "").strip()
    if not name and isinstance(history, list):
        for entry in history:
            if isinstance(entry, dict) and valid_tid(entry.get("tid")) == tid:
                name = str(entry.get("name") or "").strip()
                break
    result = [{"tid": tid, "name": name}]
    seen = {tid}
    for entry in history if isinstance(history, list) else []:
        if not isinstance(entry, dict):
            continue
        old_tid = valid_tid(entry.get("tid"))
        if not old_tid or old_tid in seen:
            continue
        seen.add(old_tid)
        result.append({"tid": old_tid, "name": str(entry.get("name") or "").strip()})
        if len(result) >= max(1, int(limit)):
            break
    return result


def clean(history, limit=MAX_HISTORY):
    """Validate and deduplicate persisted history without changing its order."""
    result = []
    seen = set()
    for entry in history if isinstance(history, list) else []:
        if not isinstance(entry, dict):
            continue
        tid = valid_tid(entry.get("tid"))
        if not tid or tid in seen:
            continue
        seen.add(tid)
        result.append({"tid": tid, "name": str(entry.get("name") or "").strip()})
        if len(result) >= max(1, int(limit)):
            break
    return result
