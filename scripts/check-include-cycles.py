#!/usr/bin/env python3
"""Detect include cycle in C/C++ headers.
Usage:
  python check-include-cycles.py --full <source-dir>
  python check-include-cycles.py --baseline <baseline.json> <source-dir>
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict, deque

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

def parse_includes(file_path):
    includes = []
    try:
        with open(file_path, encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = INCLUDE_RE.match(line)
                if m:
                    includes.append(m.group(1))
    except Exception:
        pass
    return includes

def build_graph(source_dir, extensions=('.h', '.hpp', '.cpp', '.cc')):
    graph = defaultdict(list)
    files = []
    for root, _, names in os.walk(source_dir):
        for name in names:
            if name.endswith(extensions):
                fp = os.path.join(root, name)
                files.append(fp)
                for inc in parse_includes(fp):
                    graph[fp].append(inc)
    return files, graph

def find_cycles(graph):
    cycles = []
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {n: WHITE for n in graph}
    parent = {}

    def dfs(node, path):
        color[node] = GRAY
        path.append(node)
        for neighbor in graph.get(node, []):
            if color.get(neighbor, WHITE) == GRAY:
                idx = path.index(neighbor) if neighbor in path else 0
                cycles.append(path[idx:] + [neighbor])
            elif color.get(neighbor, WHITE) == WHITE:
                dfs(neighbor, path)
        path.pop()
        color[node] = BLACK

    for node in list(graph.keys()):
        if color[node] == WHITE:
            dfs(node, [])
    return cycles

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source_dir')
    ap.add_argument('--full', action='store_true', help='Full cycle detection')
    ap.add_argument('--baseline', help='Baseline JSON for comparison')
    args = ap.parse_args()

    files, graph = build_graph(args.source_dir)
    cycles = find_cycles(graph)

    if args.baseline:
        with open(args.baseline) as f:
            baseline = json.load(f)
        baseline_cycles = set(tuple(c) for c in baseline.get('cycles', []))
        new_cycles = [c for c in cycles if tuple(c) not in baseline_cycles]
        if new_cycles:
            print(f"NEW cycles detected ({len(new_cycles)}):")
            for c in new_cycles:
                print('  ' + ' -> '.join(c))
            sys.exit(1)
        else:
            print("No new cycles detected")
    else:
        if cycles:
            print(f"Cycles detected ({len(cycles)}):")
            for c in cycles:
                print('  ' + ' -> '.join(c))
        else:
            print("No cycles detected")

if __name__ == '__main__':
    main()
