"""Exact edits for checksum-pinned generated sources; ambiguous inputs fail closed."""


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Pinned upstream changed near: {old[:80]}")
    return text.replace(old, new)


def section(text, start, end, replacement):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError("Pinned upstream section changed")
    a, b = text.index(start), text.index(end)
    if b <= a:
        raise ValueError("Pinned upstream section order changed")
    return text[:a] + replacement + text[b:]
