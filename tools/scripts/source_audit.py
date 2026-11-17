#!/usr/bin/env python3
"""Audit the SDK source subset; pruning is explicit, backed up and path checked."""
from __future__ import annotations

import argparse
from fnmatch import fnmatchcase
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / 'tools/source-policy.json'


def source_files(directory):
    for current, directories, files in os.walk(directory, followlinks=False):
        directories[:] = sorted(d for d in directories if d not in ('.git', '__pycache__')
                                and not (Path(current) / d).is_symlink())
        for name in sorted(files):
            path = Path(current) / name
            if not path.is_symlink():
                yield path


def inventory(policy):
    result = {}
    for name in policy['scope']:
        files = list(source_files(ROOT / name))
        result[name] = {'files': len(files), 'bytes': sum(p.stat().st_size for p in files)}
    return result


def protected_roots(policy):
    roots = []
    for name in policy.get('protected_roots', []):
        path = (ROOT / name).resolve()
        if not path.is_relative_to(ROOT) or path == ROOT:
            raise ValueError(f'unsafe protected root: {path}')
        roots.append(path)
    return roots


def candidates(policy):
    protected = protected_roots(policy)
    for group in policy['retention_groups']:
        directory = (ROOT / group['root']).resolve()
        if any(directory.is_relative_to(path) or path.is_relative_to(directory)
               for path in protected):
            raise ValueError(f'pruning rule overlaps protected vendor sources: {directory}')
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0'))
    changed = set()
    for command in (['git', '-c', 'core.safecrlf=false', 'diff', '--name-only', '-z'],
                    ['git', '-c', 'core.safecrlf=false', 'diff', '--cached', '--name-only', '-z']):
        changed.update(subprocess.check_output(command, cwd=ROOT).decode().split('\0'))
    result = []
    for group in policy['retention_groups']:
        directory = (ROOT / group['root']).resolve()
        if not directory.is_relative_to(ROOT) or directory == ROOT:
            raise ValueError(f'unsafe policy root: {directory}')
        for path in source_files(directory):
            local = path.relative_to(directory).as_posix()
            if 'drop' in group:
                if not any(fnmatchcase(local, pattern) for pattern in group['drop']):
                    continue
            elif any(fnmatchcase(local, pattern) for pattern in group['keep']):
                continue
            relative = path.relative_to(ROOT).as_posix()
            # Local binary/reference-output fixtures are archived too. Other untracked files may
            # be user work, so they are left in place and outside automatic trim.
            local_binary = path.suffix.lower() in ('.a', '.lib', '.elf', '.o', '.obj', '.bin',
                                                    '.hex', '.so', '.dll', '.exe', '.pdb', '.map', '.out')
            if relative not in tracked and not local_binary:
                continue
            if relative in changed:
                raise ValueError(f'candidate has pre-existing changes: {relative}')
            if not path.resolve().is_relative_to(directory):
                raise ValueError(f'candidate escapes intended root: {path}')
            result.append({'path': relative, 'root': group['root'], 'reason': group['reason'],
                           'tracked': relative in tracked,
                           'bytes': path.stat().st_size,
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    return sorted(result, key=lambda item: item['path'])


def prune(items, backup):
    protected = protected_roots(json.loads(POLICY.read_text(encoding='utf-8')))
    for item in items:
        path = (ROOT / item['path']).resolve()
        if any(path.is_relative_to(directory) for directory in protected):
            raise ValueError(f'refusing to prune protected vendor source: {path}')
    backup = backup.resolve()
    # Backup is a new file in the workspace output tree; never overwrite it.
    if not backup.is_relative_to(ROOT / 'out'):
        raise ValueError('backup must be inside the workspace out directory')
    backup.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('removal-manifest.json', json.dumps(items, indent=2))
        for item in items:
            path = ROOT / item['path']
            if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
                raise ValueError(f'candidate changed after audit: {path}')
            archive.write(path, item['path'])
    with zipfile.ZipFile(backup) as archive:
        if archive.testzip() is not None:
            raise ValueError('backup verification failed')
    for item in items:
        path = ROOT / item['path']
        intended = (ROOT / item['root']).resolve()
        if not path.resolve().is_relative_to(intended) or path.is_symlink():
            raise ValueError(f'unsafe deletion target: {path}')
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError(f'candidate changed after backup: {path}')
        path.unlink()
    # Only visit ancestors of removed files. Leave unrelated empty directories
    # and nested Git metadata intact.
    parents = set()
    for item in items:
        intended = (ROOT / item['root']).resolve()
        parent = (ROOT / item['path']).parent.resolve()
        while parent != intended and parent.is_relative_to(intended):
            parents.add(parent)
            parent = parent.parent
    for parent in sorted(parents, key=lambda path: len(path.parts), reverse=True):
        try:
            parent.rmdir()
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    policy = json.loads(POLICY.read_text(encoding='utf-8'))
    before = inventory(policy)
    items = candidates(policy)
    report = {'schema': 'rtos-sdk.source-audit.v1', 'before': before, 'removals': items}
    if args.apply:
        if args.backup is None:
            parser.error('--apply requires a new --backup path')
        prune(items, args.backup)
    report['after'] = inventory(policy)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    tracked_count = sum(item['tracked'] for item in items)
    print(f'{len(items)} removable files ({tracked_count} tracked, {len(items) - tracked_count} local artifacts), '
          f'{sum(x["bytes"] for x in items) / 1024**2:.2f} MiB')
    if not items:
        print('Source subset matches the retention policy')
    if args.check and items:
        raise SystemExit('Source subset contains files outside the retention policy')


if __name__ == '__main__':
    main()
