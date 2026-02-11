#!/usr/bin/env python3
"""
Validate that feature key lists stay in sync across build tooling.
"""

from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def parse_python_dict_keys(path: Path, variable_name: str) -> list[str]:
    tree = ast.parse(path.read_text(), filename=str(path))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == variable_name:
                    if not isinstance(node.value, ast.Dict):
                        raise ValueError(f"{path}: {variable_name} is not a dict literal")
                    keys: list[str] = []
                    for key in node.value.keys:
                        if isinstance(key, ast.Constant) and isinstance(key.value, str):
                            keys.append(key.value)
                        else:
                            raise ValueError(f"{path}: {variable_name} has a non-string key")
                    return keys
    raise ValueError(f"{path}: {variable_name} assignment not found")


def parse_python_list(path: Path, variable_name: str) -> list[str]:
    tree = ast.parse(path.read_text(), filename=str(path))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == variable_name:
                    if not isinstance(node.value, (ast.List, ast.Tuple)):
                        raise ValueError(f"{path}: {variable_name} is not a list/tuple literal")
                    keys: list[str] = []
                    for element in node.value.elts:
                        if isinstance(element, ast.Constant) and isinstance(element.value, str):
                            keys.append(element.value)
                        else:
                            raise ValueError(f"{path}: {variable_name} has a non-string element")
                    return keys
    raise ValueError(f"{path}: {variable_name} assignment not found")


def parse_shell_features(path: Path) -> list[str]:
    text = path.read_text()
    match = re.search(r"^\s*FEATURES=\(([^)]*)\)", text, re.M)
    if not match:
        raise ValueError(f"{path}: FEATURES=(...) not found")

    values: list[str] = []
    for dq, sq in re.findall(r'"([^"]+)"|\'([^\']+)\'', match.group(1)):
        values.append(dq or sq)

    if not values:
        raise ValueError(f"{path}: FEATURES list is empty")
    return values


def parse_configurator_features(path: Path) -> list[str]:
    text = path.read_text()
    marker = "const FEATURES = {"
    start = text.find(marker)
    if start == -1:
        raise ValueError(f"{path}: '{marker}' block not found")

    index = start + len(marker)
    depth = 1
    while index < len(text) and depth > 0:
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        index += 1

    if depth != 0:
        raise ValueError(f"{path}: unbalanced braces while parsing FEATURES block")

    block = text[start + len(marker) : index - 1]
    keys = re.findall(r"^\s*([a-z_][a-z0-9_]*)\s*:\s*\{", block, re.M)
    if not keys:
        raise ValueError(f"{path}: no feature keys found in FEATURES block")
    return keys


def check_duplicates(keys: list[str]) -> list[str]:
    seen: set[str] = set()
    duplicates: list[str] = []
    for key in keys:
        if key in seen:
            duplicates.append(key)
        seen.add(key)
    return duplicates


def compare_feature_sets(reference: list[str], candidate: list[str], name: str) -> list[str]:
    errors: list[str] = []
    reference_set = set(reference)
    candidate_set = set(candidate)

    missing = sorted(reference_set - candidate_set)
    extra = sorted(candidate_set - reference_set)
    if missing:
        errors.append(f"{name}: missing keys: {', '.join(missing)}")
    if extra:
        errors.append(f"{name}: unexpected keys: {', '.join(extra)}")

    duplicates = check_duplicates(candidate)
    if duplicates:
        errors.append(f"{name}: duplicate keys: {', '.join(sorted(set(duplicates)))}")

    return errors


def main() -> int:
    reference_file = ROOT / "scripts" / "generate_build_config.py"
    reference_features = parse_python_dict_keys(reference_file, "FEATURES")

    sources = {
        "measure_feature_sizes.py": parse_python_list(ROOT / "scripts" / "measure_feature_sizes.py", "FEATURES"),
        "test_all_combinations.sh": parse_shell_features(ROOT / "scripts" / "test_all_combinations.sh"),
        "docs/configurator/index.html": parse_configurator_features(ROOT / "docs" / "configurator" / "index.html"),
    }

    errors: list[str] = []
    reference_duplicates = check_duplicates(reference_features)
    if reference_duplicates:
        errors.append(
            "generate_build_config.py: duplicate keys: "
            + ", ".join(sorted(set(reference_duplicates)))
        )

    for name, keys in sources.items():
        errors.extend(compare_feature_sets(reference_features, keys, name))

    if errors:
        print("Feature key synchronization check failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(
        "Feature key synchronization check passed: "
        f"{len(reference_features)} keys are aligned across tooling."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
